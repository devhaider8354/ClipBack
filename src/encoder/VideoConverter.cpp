#include "encoder/VideoConverter.h"
#include "logging/Logger.h"

namespace clipback {

VideoConverter::~VideoConverter() { Shutdown(); }

bool VideoConverter::Initialize(D3DDevice& device, UINT outputWidth, UINT outputHeight, UINT fps, UINT poolSize, std::wstring& error)
{
    Shutdown();
    if (!device.VideoApiAvailable()) {
        error = L"ID3D11VideoDevice not available on this adapter (driver lacks video processing support)";
        return false;
    }
    device_ = &device;
    outW_ = outputWidth & ~1u;
    outH_ = outputHeight & ~1u;
    fps_ = fps;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = outW_;
    td.Height = outH_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_NV12;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // Hardware encoders (MF) may need to open the surface from their own device.
    td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    for (UINT i = 0; i < poolSize; ++i) {
        ComPtr<ID3D11Texture2D> tex;
        HRESULT hr = device_->Device()->CreateTexture2D(&td, nullptr, &tex);
        if (FAILED(hr) && (td.MiscFlags & D3D11_RESOURCE_MISC_SHARED)) {
            td.MiscFlags = 0;
            hr = device_->Device()->CreateTexture2D(&td, nullptr, &tex);
        }
        if (FAILED(hr)) {
            error = L"CreateTexture2D(NV12) failed: " + logging::HResultToString(hr);
            Shutdown();
            return false;
        }
        pool_.push_back(tex);
    }
    LOG_INFO(L"Video converter ready: output %ux%u NV12, pool=%u", outW_, outH_, poolSize);
    return true;
}

void VideoConverter::Shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    outputViews_.clear();
    processor_.Reset();
    enumerator_.Reset();
    pool_.clear();
    intermediate_.Reset();
    intermediateW_ = intermediateH_ = 0;
    inW_ = inH_ = 0;
    next_ = 0;
    device_ = nullptr;
}

bool VideoConverter::CreateProcessor(UINT inW, UINT inH, std::wstring& error)
{
    outputViews_.clear();
    processor_.Reset();
    enumerator_.Reset();

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputFrameRate.Numerator = fps_;
    desc.InputFrameRate.Denominator = 1;
    desc.InputWidth = inW;
    desc.InputHeight = inH;
    desc.OutputFrameRate.Numerator = fps_;
    desc.OutputFrameRate.Denominator = 1;
    desc.OutputWidth = outW_;
    desc.OutputHeight = outH_;
    desc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;

    HRESULT hr = device_->VideoDevice()->CreateVideoProcessorEnumerator(&desc, &enumerator_);
    if (FAILED(hr)) { error = L"CreateVideoProcessorEnumerator failed: " + logging::HResultToString(hr); return false; }

    UINT flags = 0;
    if (FAILED(enumerator_->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &flags)) || !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
        error = L"Video processor cannot output NV12";
        return false;
    }

    hr = device_->VideoDevice()->CreateVideoProcessor(enumerator_.Get(), 0, &processor_);
    if (FAILED(hr)) { error = L"CreateVideoProcessor failed: " + logging::HResultToString(hr); return false; }

    ID3D11VideoContext* vc = device_->VideoContext();

    // Colour spaces: input is full-range sRGB, output is BT.709 studio-range YCbCr (what every player expects).
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE inCs{};
    inCs.Usage = 0;
    inCs.RGB_Range = 0;          // full range
    inCs.YCbCr_Matrix = 1;       // BT.709
    inCs.YCbCr_xvYCC = 0;
    inCs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
    vc->VideoProcessorSetStreamColorSpace(processor_.Get(), 0, &inCs);

    D3D11_VIDEO_PROCESSOR_COLOR_SPACE outCs{};
    outCs.Usage = 0;
    outCs.RGB_Range = 0;
    outCs.YCbCr_Matrix = 1;
    outCs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    vc->VideoProcessorSetOutputColorSpace(processor_.Get(), &outCs);

    RECT src{ 0, 0, static_cast<LONG>(inW), static_cast<LONG>(inH) };
    RECT dst{ 0, 0, static_cast<LONG>(outW_), static_cast<LONG>(outH_) };
    vc->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE, &src);
    vc->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &dst);
    vc->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
    vc->VideoProcessorSetStreamAutoProcessingMode(processor_.Get(), 0, FALSE);   // no deinterlace/denoise filters = lowest GPU cost

    for (auto& tex : pool_) {
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd{};
        ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ovd.Texture2D.MipSlice = 0;
        ComPtr<ID3D11VideoProcessorOutputView> view;
        hr = device_->VideoDevice()->CreateVideoProcessorOutputView(tex.Get(), enumerator_.Get(), &ovd, &view);
        if (FAILED(hr)) { error = L"CreateVideoProcessorOutputView failed: " + logging::HResultToString(hr); return false; }
        outputViews_.push_back(view);
    }

    inW_ = inW;
    inH_ = inH;
    LOG_INFO(L"Video processor created: %ux%u BGRA -> %ux%u NV12", inW, inH, outW_, outH_);
    return true;
}

ID3D11Texture2D* VideoConverter::Convert(ID3D11Texture2D* source, UINT sourceWidth, UINT sourceHeight, std::wstring& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || pool_.empty()) { error = L"Converter not initialised"; return nullptr; }
    if (sourceWidth == 0 || sourceHeight == 0) { error = L"Empty source frame"; return nullptr; }

    if (sourceWidth != inW_ || sourceHeight != inH_ || !processor_) {
        if (!CreateProcessor(sourceWidth, sourceHeight, error)) return nullptr;
    }

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
    ivd.FourCC = 0;
    ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    ivd.Texture2D.MipSlice = 0;
    ivd.Texture2D.ArraySlice = 0;
    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hr = device_->VideoDevice()->CreateVideoProcessorInputView(source, enumerator_.Get(), &ivd, &inputView);
    if (FAILED(hr)) {
        // Some drivers refuse input views on textures they did not allocate with render-target/shared flags.
        // Copy into our own intermediate texture (one GPU copy, still no CPU involvement) and retry.
        if (!intermediate_ || intermediateW_ != sourceWidth || intermediateH_ != sourceHeight) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = sourceWidth;
            td.Height = sourceHeight;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            intermediate_.Reset();
            HRESULT chr = device_->Device()->CreateTexture2D(&td, nullptr, &intermediate_);
            if (FAILED(chr)) { error = L"CreateTexture2D(intermediate BGRA) failed: " + logging::HResultToString(chr); return nullptr; }
            intermediateW_ = sourceWidth;
            intermediateH_ = sourceHeight;
        }
        D3D11_BOX box{ 0, 0, 0, sourceWidth, sourceHeight, 1 };
        device_->Context()->CopySubresourceRegion(intermediate_.Get(), 0, 0, 0, 0, source, 0, &box);
        hr = device_->VideoDevice()->CreateVideoProcessorInputView(intermediate_.Get(), enumerator_.Get(), &ivd, &inputView);
        if (FAILED(hr)) { error = L"CreateVideoProcessorInputView failed: " + logging::HResultToString(hr); return nullptr; }
    }

    const size_t index = next_;
    next_ = (next_ + 1) % pool_.size();

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.OutputIndex = 0;
    stream.InputFrameOrField = 0;
    stream.pInputSurface = inputView.Get();

    hr = device_->VideoContext()->VideoProcessorBlt(processor_.Get(), outputViews_[index].Get(), 0, 1, &stream);
    if (FAILED(hr)) { error = L"VideoProcessorBlt failed: " + logging::HResultToString(hr); return nullptr; }
    return pool_[index].Get();
}

} // namespace clipback
