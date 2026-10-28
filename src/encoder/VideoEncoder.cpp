#include "encoder/VideoEncoder.h"
#include "encoder/H264Utils.h"
#include "logging/Logger.h"
#include "common/StringUtils.h"

#include <mfapi.h>
#include <mfobjects.h>
#include <strmif.h>
#include <initguid.h>     // defines the CODECAPI_* GUIDs in this translation unit
#include <codecapi.h>
#include <avrt.h>
#include <algorithm>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "strmiids.lib")
#include <iterator>

namespace clipback {

// ---------------------------------------------------------------------------
// Async MFT event callback
// ---------------------------------------------------------------------------

class VideoEncoder::EventCallback final : public IMFAsyncCallback {
public:
    explicit EventCallback(VideoEncoder* owner) : owner_(owner) {}

    void Detach()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        owner_ = nullptr;
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFAsyncCallback)) {
            *ppv = static_cast<IMFAsyncCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }

    // IMFAsyncCallback
    STDMETHODIMP GetParameters(DWORD*, DWORD*) override { return E_NOTIMPL; }
    STDMETHODIMP Invoke(IMFAsyncResult* result) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!owner_ || !owner_->eventGen_) return S_OK;

        ComPtr<IMFMediaEvent> event;
        HRESULT hr = owner_->eventGen_->EndGetEvent(result, &event);
        if (SUCCEEDED(hr) && event) {
            MediaEventType type = MEUnknown;
            HRESULT status = S_OK;
            event->GetType(&type);
            event->GetStatus(&status);
            owner_->OnEvent(type, status);
        }
        // Re-arm unless the encoder is shutting down.
        if (!owner_->stop_) owner_->eventGen_->BeginGetEvent(this, nullptr);
        return S_OK;
    }

private:
    ~EventCallback() = default;
    VideoEncoder* owner_;
    std::mutex mutex_;
    volatile LONG refs_ = 1;
};

// ---------------------------------------------------------------------------
// Construction / selection
// ---------------------------------------------------------------------------

VideoEncoder::VideoEncoder() = default;
VideoEncoder::~VideoEncoder() { Shutdown(); }

std::vector<VideoEncoder::Candidate> VideoEncoder::EnumerateCandidates()
{
    std::vector<Candidate> result;
    MFT_REGISTER_TYPE_INFO inType{ MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO outType{ MFMediaType_Video, MFVideoFormat_H264 };
    const UINT32 flagSets[] = {
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
    };
    for (UINT32 flags : flagSets) {
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        if (FAILED(MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &inType, &outType, &activates, &count))) continue;
        for (UINT32 i = 0; i < count; ++i) {
            Candidate c;
            c.activate.Attach(activates[i]);   // take ownership
            LPWSTR name = nullptr; UINT32 len = 0;
            if (SUCCEEDED(c.activate->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)) && name) { c.info.friendlyName = name; CoTaskMemFree(name); }
            LPWSTR ven = nullptr;
            if (SUCCEEDED(c.activate->GetAllocatedString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, &ven, &len)) && ven) { c.info.vendorIdString = ven; CoTaskMemFree(ven); }
            UINT32 mftFlags = 0;
            c.activate->GetUINT32(MF_TRANSFORM_FLAGS_Attribute, &mftFlags);
            c.info.hardware = (flags & MFT_ENUM_FLAG_HARDWARE) != 0 || (mftFlags & MFT_ENUM_FLAG_HARDWARE) != 0;
            c.info.async = (mftFlags & MFT_ENUM_FLAG_ASYNCMFT) != 0 || c.info.hardware;
            if (str::ContainsNoCase(c.info.vendorIdString, L"10DE")) c.info.vendor = GpuVendor::Nvidia;
            else if (str::ContainsNoCase(c.info.vendorIdString, L"1002")) c.info.vendor = GpuVendor::Amd;
            else if (str::ContainsNoCase(c.info.vendorIdString, L"8086")) c.info.vendor = GpuVendor::Intel;
            else c.info.vendor = VendorFromName(c.info.friendlyName);
            bool dup = false;
            for (auto& e : result) if (e.info.friendlyName == c.info.friendlyName) dup = true;
            if (!dup) result.push_back(std::move(c));
        }
        CoTaskMemFree(activates);
    }
    return result;
}

std::vector<VideoEncoder::Candidate> VideoEncoder::OrderCandidates(std::vector<Candidate> all)
{
    auto rank = [&](const Candidate& c) -> int {
        const EncoderPreference p = config_.preference;
        if (p == EncoderPreference::Cpu) return c.info.hardware ? 100 : 0;
        if (p == EncoderPreference::Nvenc) { if (c.info.vendor == GpuVendor::Nvidia && c.info.hardware) return 0; }
        if (p == EncoderPreference::Amf)   { if (c.info.vendor == GpuVendor::Amd && c.info.hardware) return 0; }
        if (p == EncoderPreference::Qsv)   { if (c.info.vendor == GpuVendor::Intel && c.info.hardware) return 0; }
        // Auto (and fallback ordering for explicit choices that are missing):
        if (c.info.hardware) {
            if (c.info.vendor == config_.preferredVendor && config_.preferredVendor != GpuVendor::Unknown) return 10;
            if (c.info.vendor == GpuVendor::Nvidia) return 11;
            if (c.info.vendor == GpuVendor::Amd) return 12;
            if (c.info.vendor == GpuVendor::Intel) return 13;
            return 14;
        }
        return 50;   // software last
    };
    std::stable_sort(all.begin(), all.end(), [&](const Candidate& a, const Candidate& b) { return rank(a) < rank(b); });
    return all;
}

bool VideoEncoder::Initialize(D3DDevice& device, const VideoEncoderConfig& config, PacketCallback onPacket, std::wstring& error)
{
    Shutdown();
    device_ = &device;
    config_ = config;
    config_.width &= ~1u;
    config_.height &= ~1u;
    onPacket_ = std::move(onPacket);
    stop_ = false;
    needInput_ = haveOutput_ = 0;
    firstOutput_ = true;
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = {};
        failure_.clear();
    }

    std::vector<Candidate> candidates = OrderCandidates(EnumerateCandidates());
    if (candidates.empty()) {
        error = L"No H.264 encoder (hardware or software) is available on this system";
        return false;
    }

    std::wstring lastError;
    for (auto& c : candidates) {
        if (config_.preference == EncoderPreference::Cpu && c.info.hardware) continue;
        std::wstring err;
        if (TryConfigure(c, err)) {
            selected_ = c.info;
            break;
        }
        LOG_WARN(L"Encoder '%s' rejected: %s", c.info.friendlyName.c_str(), err.c_str());
        lastError = c.info.friendlyName + L": " + err;
        ReleaseTransform();
    }
    if (!mft_) {
        error = L"Could not initialise any H.264 encoder. Last error: " + lastError;
        return false;
    }

    const bool explicitVendorMissed =
        (config_.preference == EncoderPreference::Nvenc && selected_.vendor != GpuVendor::Nvidia) ||
        (config_.preference == EncoderPreference::Amf && selected_.vendor != GpuVendor::Amd) ||
        (config_.preference == EncoderPreference::Qsv && selected_.vendor != GpuVendor::Intel);
    if (explicitVendorMissed) LOG_WARN(L"Requested encoder vendor not available - using '%s' instead", selected_.friendlyName.c_str());

    LOG_INFO(L"Encoder: %s (%s, %s, d3d-aware=%d) %ux%u@%u %u kbps", selected_.friendlyName.c_str(),
        selected_.hardware ? L"hardware" : L"software", async_ ? L"async" : L"sync", d3dAware_ ? 1 : 0,
        config_.width, config_.height, config_.fps, config_.bitrateKbps);

    thread_ = std::thread(&VideoEncoder::ThreadMain, this);
    return true;
}

void VideoEncoder::ReleaseTransform()
{
    if (eventCallback_) { eventCallback_->Detach(); eventCallback_.Reset(); }
    eventGen_.Reset();
    if (mft_) {
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        mft_.Reset();
    }
    staging_.Reset();
}

void VideoEncoder::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    ReleaseTransform();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(typeMutex_);
        outputType_.Reset();
    }
    onPacket_ = nullptr;
    device_ = nullptr;
}

// ---------------------------------------------------------------------------
// MFT configuration
// ---------------------------------------------------------------------------

static void SetCodecUI4(ICodecAPI* api, const GUID& key, ULONG value, const wchar_t* name)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_UI4;
    v.ulVal = value;
    const HRESULT hr = api->SetValue(&key, &v);
    if (FAILED(hr)) LOG_DEBUG(L"ICodecAPI %s not applied: %s", name, logging::HResultToString(hr).c_str());
}

static void SetCodecBool(ICodecAPI* api, const GUID& key, bool value, const wchar_t* name)
{
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    const HRESULT hr = api->SetValue(&key, &v);
    if (FAILED(hr)) LOG_DEBUG(L"ICodecAPI %s not applied: %s", name, logging::HResultToString(hr).c_str());
}

void VideoEncoder::ApplyCodecSettings()
{
    ComPtr<ICodecAPI> api;
    if (FAILED(mft_.As(&api)) || !api) return;
    SetCodecUI4(api.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR, L"RateControlMode");
    SetCodecUI4(api.Get(), CODECAPI_AVEncCommonMeanBitRate, config_.bitrateKbps * 1000u, L"MeanBitRate");
    SetCodecUI4(api.Get(), CODECAPI_AVEncMPVGOPSize, config_.fps * 2u, L"GOPSize");            // 2 s GOP -> replay start granularity
    SetCodecUI4(api.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0, L"BPictureCount");         // no B-frames: pts == dts, lower latency
    SetCodecBool(api.Get(), CODECAPI_AVEncCommonLowLatency, true, L"CommonLowLatency");
    SetCodecBool(api.Get(), CODECAPI_AVLowLatencyMode, true, L"LowLatencyMode");
    SetCodecUI4(api.Get(), CODECAPI_AVEncCommonQualityVsSpeed, 33, L"QualityVsSpeed");         // favour speed on the GPU
}

bool VideoEncoder::ConfigureTypes(std::wstring& error)
{
    HRESULT hr;
    // Output type: H.264
    const UINT32 profiles[] = { eAVEncH264VProfile_High, eAVEncH264VProfile_Main, eAVEncH264VProfile_Base };
    bool outputSet = false;
    for (UINT32 profile : profiles) {
        ComPtr<IMFMediaType> out;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        out->SetUINT32(MF_MT_AVG_BITRATE, config_.bitrateKbps * 1000u);
        MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, config_.width, config_.height);
        MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, config_.fps, 1);
        MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        out->SetUINT32(MF_MT_MPEG2_PROFILE, profile);
        hr = mft_->SetOutputType(outputId_, out.Get(), 0);
        if (SUCCEEDED(hr)) { outputSet = true; break; }
    }
    if (!outputSet) { error = L"SetOutputType(H264) rejected"; return false; }

    // Input type: NV12. Prefer the MFT's own enumerated type so it carries driver-specific attributes.
    ComPtr<IMFMediaType> input;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> t;
        hr = mft_->GetInputAvailableType(inputId_, i, &t);
        if (FAILED(hr)) break;
        GUID sub{};
        if (SUCCEEDED(t->GetGUID(MF_MT_SUBTYPE, &sub)) && sub == MFVideoFormat_NV12) { input = t; break; }
    }
    if (!input) {
        MFCreateMediaType(&input);
        input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    }
    MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, config_.width, config_.height);
    MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, config_.fps, 1);
    MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    input->SetUINT32(MF_MT_DEFAULT_STRIDE, config_.width);
    input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    hr = mft_->SetInputType(inputId_, input.Get(), 0);
    if (FAILED(hr)) { error = L"SetInputType(NV12) failed: " + logging::HResultToString(hr); return false; }

    MFT_OUTPUT_STREAM_INFO info{};
    hr = mft_->GetOutputStreamInfo(outputId_, &info);
    if (FAILED(hr)) { error = L"GetOutputStreamInfo failed: " + logging::HResultToString(hr); return false; }
    providesSamples_ = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    outputBufferSize_ = info.cbSize ? info.cbSize : (config_.width * config_.height * 3 / 2);
    return true;
}

bool VideoEncoder::TryConfigure(Candidate& c, std::wstring& error)
{
    HRESULT hr = c.activate->ActivateObject(IID_PPV_ARGS(&mft_));
    if (FAILED(hr)) { error = L"ActivateObject failed: " + logging::HResultToString(hr); return false; }

    ComPtr<IMFAttributes> attrs;
    async_ = false;
    d3dAware_ = false;
    if (SUCCEEDED(mft_->GetAttributes(&attrs)) && attrs) {
        UINT32 v = 0;
        if (SUCCEEDED(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &v)) && v) {
            async_ = true;
            attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        }
        v = 0;
        if (SUCCEEDED(attrs->GetUINT32(MF_SA_D3D11_AWARE, &v)) && v) d3dAware_ = true;
        // Some hardware MFTs report MF_SA_D3D11_AWARE only after the D3D manager is set: hardware => try.
        if (c.info.hardware) d3dAware_ = true;
    }

    DWORD inIds[1] = { 0 }, outIds[1] = { 0 };
    hr = mft_->GetStreamIDs(1, inIds, 1, outIds);
    if (hr == E_NOTIMPL) { inIds[0] = 0; outIds[0] = 0; }
    inputId_ = inIds[0];
    outputId_ = outIds[0];

    if (d3dAware_) {
        hr = mft_->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(device_->DeviceManager()));
        if (FAILED(hr)) {
            LOG_WARN(L"'%s' refused the D3D11 device manager (%s) - falling back to CPU readback for this encoder", c.info.friendlyName.c_str(), logging::HResultToString(hr).c_str());
            d3dAware_ = false;
        }
    }

    ApplyCodecSettings();
    if (!ConfigureTypes(error)) return false;
    ApplyCodecSettings();   // some drivers only accept rate-control changes once types are negotiated

    if (!d3dAware_) {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = config_.width;
        td.Height = config_.height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_NV12;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device_->Device()->CreateTexture2D(&td, nullptr, &staging_);
        if (FAILED(hr)) { error = L"CreateTexture2D(staging NV12) failed: " + logging::HResultToString(hr); return false; }
    }

    if (async_) {
        hr = mft_.As(&eventGen_);
        if (FAILED(hr) || !eventGen_) { error = L"Async MFT without IMFMediaEventGenerator"; return false; }
        eventCallback_.Attach(new EventCallback(this));
        hr = eventGen_->BeginGetEvent(eventCallback_.Get(), nullptr);
        if (FAILED(hr)) { error = L"BeginGetEvent failed: " + logging::HResultToString(hr); return false; }
    }

    hr = mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (FAILED(hr)) { error = L"NOTIFY_BEGIN_STREAMING failed: " + logging::HResultToString(hr); return false; }
    hr = mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    if (FAILED(hr)) { error = L"NOTIFY_START_OF_STREAM failed: " + logging::HResultToString(hr); return false; }
    return true;
}

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------

void VideoEncoder::OnEvent(MediaEventType type, HRESULT status)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (type) {
        case METransformNeedInput: ++needInput_; break;
        case METransformHaveOutput: ++haveOutput_; break;
        case MEError:
            SetFailure(L"Encoder reported MEError: " + logging::HResultToString(status));
            break;
        default: break;
        }
    }
    cv_.notify_all();
}

void VideoEncoder::SetFailure(const std::wstring& msg)
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    if (failure_.empty()) {
        failure_ = msg;
        LOG_ERROR(L"%s", msg.c_str());
    }
}

bool VideoEncoder::HasFailed(std::wstring& error) const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    error = failure_;
    return !failure_.empty();
}

VideoEncoderStats VideoEncoder::Stats() const
{
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

ComPtr<IMFMediaType> VideoEncoder::OutputType() const
{
    std::lock_guard<std::mutex> lock(typeMutex_);
    return outputType_;
}

void VideoEncoder::Submit(ID3D11Texture2D* nv12, int64_t pts, int64_t duration)
{
    if (!nv12) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        if (queue_.size() >= maxQueue_) {
            // Encoder is behind: keep the newest frame, drop the oldest (never block the capture thread).
            queue_.pop_front();
            std::lock_guard<std::mutex> s(statsMutex_);
            ++stats_.framesDropped;
        }
        queue_.push_back(PendingFrame{ ComPtr<ID3D11Texture2D>(nv12), pts, duration });
        std::lock_guard<std::mutex> s(statsMutex_);
        ++stats_.framesSubmitted;
    }
    cv_.notify_all();
}

ComPtr<IMFSample> VideoEncoder::MakeSampleFromTexture(const PendingFrame& frame)
{
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), frame.texture.Get(), 0, FALSE, &buffer);
    if (FAILED(hr)) { SetFailure(L"MFCreateDXGISurfaceBuffer failed: " + logging::HResultToString(hr)); return nullptr; }
    ComPtr<IMF2DBuffer> b2d;
    if (SUCCEEDED(buffer.As(&b2d)) && b2d) {
        DWORD len = 0;
        if (SUCCEEDED(b2d->GetContiguousLength(&len))) buffer->SetCurrentLength(len);
    }
    ComPtr<IMFSample> sample;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(frame.pts);
    sample->SetSampleDuration(frame.duration);
    return sample;
}

ComPtr<IMFSample> VideoEncoder::MakeSampleByReadback(const PendingFrame& frame)
{
    ID3D11DeviceContext* ctx = device_->Context();
    ctx->CopyResource(staging_.Get(), frame.texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    HRESULT hr = ctx->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) { SetFailure(L"Map(staging) failed: " + logging::HResultToString(hr)); return nullptr; }

    const UINT w = config_.width, h = config_.height;
    const DWORD size = w * h * 3 / 2;
    ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(size, &buffer);
    if (FAILED(hr)) { ctx->Unmap(staging_.Get(), 0); SetFailure(L"MFCreateMemoryBuffer failed"); return nullptr; }
    BYTE* dst = nullptr;
    DWORD maxLen = 0;
    buffer->Lock(&dst, &maxLen, nullptr);
    const BYTE* src = static_cast<const BYTE*>(map.pData);
    for (UINT y = 0; y < h; ++y) memcpy(dst + y * w, src + y * map.RowPitch, w);                 // Y plane
    const BYTE* uvSrc = src + static_cast<size_t>(map.RowPitch) * h;
    BYTE* uvDst = dst + static_cast<size_t>(w) * h;
    for (UINT y = 0; y < h / 2; ++y) memcpy(uvDst + y * w, uvSrc + y * map.RowPitch, w);          // interleaved UV plane
    buffer->Unlock();
    buffer->SetCurrentLength(size);
    ctx->Unmap(staging_.Get(), 0);

    ComPtr<IMFSample> sample;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(frame.pts);
    sample->SetSampleDuration(frame.duration);
    return sample;
}

HRESULT VideoEncoder::FeedInput(const PendingFrame& frame)
{
    ComPtr<IMFSample> sample = d3dAware_ ? MakeSampleFromTexture(frame) : MakeSampleByReadback(frame);
    if (!sample) return E_FAIL;
    return mft_->ProcessInput(inputId_, sample.Get(), 0);
}

HRESULT VideoEncoder::ProcessOutputOnce(bool& produced)
{
    produced = false;
    MFT_OUTPUT_DATA_BUFFER out{};
    out.dwStreamID = outputId_;
    ComPtr<IMFSample> ownSample;
    if (!providesSamples_) {
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = MFCreateMemoryBuffer(outputBufferSize_, &buffer);
        if (FAILED(hr)) return hr;
        MFCreateSample(&ownSample);
        ownSample->AddBuffer(buffer.Get());
        out.pSample = ownSample.Get();
    }
    DWORD status = 0;
    HRESULT hr = mft_->ProcessOutput(0, 1, &out, &status);
    if (out.pEvents) { out.pEvents->Release(); out.pEvents = nullptr; }

    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> t;
        if (SUCCEEDED(mft_->GetOutputAvailableType(outputId_, 0, &t)) && t) {
            mft_->SetOutputType(outputId_, t.Get(), 0);
            std::lock_guard<std::mutex> lock(typeMutex_);
            outputType_.Reset();
            firstOutput_ = true;
        }
        return hr;
    }
    if (FAILED(hr)) {
        if (providesSamples_ && out.pSample) out.pSample->Release();
        return hr;
    }
    ComPtr<IMFSample> sample;
    if (providesSamples_) sample.Attach(out.pSample); else sample = ownSample;
    if (sample) {
        HandleOutputSample(sample.Get());
        produced = true;
    }
    return S_OK;
}

void VideoEncoder::HandleOutputSample(IMFSample* sample)
{
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || !buffer) return;
    BYTE* data = nullptr;
    DWORD maxLen = 0, len = 0;
    if (FAILED(buffer->Lock(&data, &maxLen, &len))) return;
    auto bytes = std::make_shared<std::vector<uint8_t>>(data, data + len);
    buffer->Unlock();
    if (bytes->empty()) return;

    EncodedPacket pkt;
    pkt.data = bytes;
    if (FAILED(sample->GetSampleTime(&pkt.pts))) pkt.pts = 0;
    if (FAILED(sample->GetSampleDuration(&pkt.duration)) || pkt.duration <= 0) pkt.duration = kTicksPerSecond / config_.fps;
    UINT32 clean = 0;
    if (SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &clean))) pkt.keyframe = clean != 0;
    else pkt.keyframe = h264::ContainsIdr(bytes->data(), bytes->size());
    if (!pkt.keyframe && h264::ContainsIdr(bytes->data(), bytes->size())) pkt.keyframe = true;

    if (firstOutput_ || !outputType_) {
        // Capture the negotiated output type and make sure it carries SPS/PPS for the MP4 muxer.
        ComPtr<IMFMediaType> t;
        if (SUCCEEDED(mft_->GetOutputCurrentType(outputId_, &t)) && t) {
            UINT32 blobSize = 0;
            if (FAILED(t->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &blobSize)) || blobSize == 0) {
                std::vector<uint8_t> header;
                if (h264::ExtractParameterSets(bytes->data(), bytes->size(), header)) {
                    t->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header.data(), static_cast<UINT32>(header.size()));
                } else {
                    LOG_WARN(L"First encoded packet has no SPS/PPS - MP4 muxing will rely on in-band parameter sets");
                }
            }
            std::lock_guard<std::mutex> lock(typeMutex_);
            outputType_ = t;
        }
        firstOutput_ = false;
    }

    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        ++stats_.framesEncoded;
    }
    if (onPacket_) onPacket_(std::move(pkt));
}

void VideoEncoder::ThreadMain()
{
    ComInitializer com;
    DWORD taskIndex = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Playback", &taskIndex);

    for (;;) {
        PendingFrame frame{};
        bool doInput = false, doOutput = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] {
                if (stop_) return true;
                if (async_) return haveOutput_ > 0 || (needInput_ > 0 && !queue_.empty());
                return !queue_.empty();
            });
            if (stop_) break;
            if (async_) {
                if (haveOutput_ > 0) { --haveOutput_; doOutput = true; }
                else { --needInput_; frame = queue_.front(); queue_.pop_front(); doInput = true; }
            } else {
                frame = queue_.front(); queue_.pop_front(); doInput = true;
            }
        }

        if (doOutput) {
            bool produced = false;
            const HRESULT hr = ProcessOutputOnce(produced);
            if (FAILED(hr) && hr != MF_E_TRANSFORM_NEED_MORE_INPUT && hr != MF_E_TRANSFORM_STREAM_CHANGE) {
                SetFailure(L"Encoder ProcessOutput failed: " + logging::HResultToString(hr));
            }
            continue;
        }

        if (doInput) {
            HRESULT hr = FeedInput(frame);
            if (!async_) {
                // Synchronous MFT: drain everything it has, then retry the input if it was refused.
                int guard = 0;
                while (guard++ < 64) {
                    bool produced = false;
                    const HRESULT ohr = ProcessOutputOnce(produced);
                    if (ohr == MF_E_TRANSFORM_NEED_MORE_INPUT) break;
                    if (FAILED(ohr) && ohr != MF_E_TRANSFORM_STREAM_CHANGE) { SetFailure(L"Encoder ProcessOutput failed: " + logging::HResultToString(ohr)); break; }
                }
                if (hr == MF_E_NOTACCEPTING) hr = FeedInput(frame);
            }
            if (FAILED(hr) && hr != MF_E_NOTACCEPTING) {
                SetFailure(L"Encoder ProcessInput failed: " + logging::HResultToString(hr));
            } else if (hr == MF_E_NOTACCEPTING) {
                std::lock_guard<std::mutex> s(statsMutex_);
                ++stats_.framesDropped;
            }
        }
    }

    if (avrt) AvRevertMmThreadCharacteristics(avrt);
}

} // namespace clipback
