#include "audio/AacEncoder.h"
#include "audio/WasapiCapture.h"   // kAudioSampleRate / kAudioChannels
#include "logging/Logger.h"
#include <mfapi.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace clipback {

AacEncoder::~AacEncoder() { Shutdown(); }

bool AacEncoder::Initialize(UINT bitrateKbps, std::function<void(EncodedPacket&&)> onPacket, std::wstring& error)
{
    Shutdown();
    onPacket_ = std::move(onPacket);

    MFT_REGISTER_TYPE_INFO inType{ MFMediaType_Audio, MFAudioFormat_PCM };
    MFT_REGISTER_TYPE_INFO outType{ MFMediaType_Audio, MFAudioFormat_AAC };
    IMFActivate** activates = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFTEnumEx(MFT_CATEGORY_AUDIO_ENCODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, &inType, &outType, &activates, &count);
    if (FAILED(hr) || count == 0) {
        error = L"No AAC encoder MFT available: " + logging::HResultToString(hr);
        if (activates) CoTaskMemFree(activates);
        return false;
    }
    hr = activates[0]->ActivateObject(IID_PPV_ARGS(&mft_));
    for (UINT32 i = 0; i < count; ++i) activates[i]->Release();
    CoTaskMemFree(activates);
    if (FAILED(hr)) { error = L"AAC encoder activation failed: " + logging::HResultToString(hr); return false; }

    // Input: PCM 16-bit 48 kHz stereo
    ComPtr<IMFMediaType> input;
    MFCreateMediaType(&input);
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate);
    input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels);
    input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 2 * kAudioChannels);
    input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioSampleRate * 2 * kAudioChannels);
    input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

    // Output: AAC. Valid bytes/sec for the Microsoft encoder are 12000, 16000, 20000, 24000 (per channel pair).
    UINT32 wantedBytesPerSec = (bitrateKbps * 1000) / 8;
    const UINT32 allowed[] = { 12000, 16000, 20000, 24000 };
    UINT32 chosen = allowed[0];
    for (UINT32 a : allowed) if (a <= wantedBytesPerSec) chosen = a;

    ComPtr<IMFMediaType> output;
    MFCreateMediaType(&output);
    output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate);
    output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels);
    output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, chosen);
    output->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);                       // raw AAC (what MP4 expects)
    output->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);  // AAC-LC, level 2

    // The Microsoft AAC encoder wants the input type first, then the output type.
    hr = mft_->SetInputType(0, input.Get(), 0);
    if (FAILED(hr)) { error = L"AAC SetInputType failed: " + logging::HResultToString(hr); return false; }
    hr = mft_->SetOutputType(0, output.Get(), 0);
    if (FAILED(hr)) {
        // Pick whatever the encoder offers that matches our rate/channels.
        bool ok = false;
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            if (FAILED(mft_->GetOutputAvailableType(0, i, &t))) break;
            UINT32 rate = 0, ch = 0;
            t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
            t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
            if (rate == kAudioSampleRate && ch == kAudioChannels && SUCCEEDED(mft_->SetOutputType(0, t.Get(), 0))) { ok = true; break; }
        }
        if (!ok) { error = L"AAC SetOutputType failed: " + logging::HResultToString(hr); return false; }
    }

    hr = mft_->GetOutputCurrentType(0, &outputType_);
    if (FAILED(hr)) { error = L"AAC GetOutputCurrentType failed: " + logging::HResultToString(hr); return false; }

    MFT_OUTPUT_STREAM_INFO info{};
    mft_->GetOutputStreamInfo(0, &info);
    providesSamples_ = (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    outputBufferSize_ = info.cbSize ? info.cbSize : 16384;

    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    framesFed_ = 0;
    outputFrames_ = 0;
    basePts_ = -1;
    LOG_INFO(L"AAC encoder ready: %u kbps", chosen * 8 / 1000);
    return true;
}

void AacEncoder::Shutdown()
{
    if (mft_) {
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        mft_.Reset();
    }
    outputType_.Reset();
    onPacket_ = nullptr;
}

bool AacEncoder::Encode(const int16_t* pcm, size_t frames, int64_t pts)
{
    if (!mft_ || frames == 0) return false;
    if (basePts_ < 0) basePts_ = pts;

    const DWORD bytes = static_cast<DWORD>(frames * kAudioChannels * sizeof(int16_t));
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(MFCreateMemoryBuffer(bytes, &buffer))) return false;
    BYTE* dst = nullptr;
    buffer->Lock(&dst, nullptr, nullptr);
    memcpy(dst, pcm, bytes);
    buffer->Unlock();
    buffer->SetCurrentLength(bytes);

    ComPtr<IMFSample> sample;
    MFCreateSample(&sample);
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(pts);
    sample->SetSampleDuration(static_cast<int64_t>(frames) * kTicksPerSecond / kAudioSampleRate);

    HRESULT hr = mft_->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        DrainOutput();
        hr = mft_->ProcessInput(0, sample.Get(), 0);
    }
    if (FAILED(hr)) {
        LOG_HR(L"AAC ProcessInput failed", hr);
        return false;
    }
    framesFed_ += static_cast<int64_t>(frames);
    DrainOutput();
    return true;
}

void AacEncoder::DrainOutput()
{
    for (int guard = 0; guard < 64; ++guard) {
        MFT_OUTPUT_DATA_BUFFER out{};
        ComPtr<IMFSample> own;
        if (!providesSamples_) {
            ComPtr<IMFMediaBuffer> buffer;
            MFCreateMemoryBuffer(outputBufferSize_, &buffer);
            MFCreateSample(&own);
            own->AddBuffer(buffer.Get());
            out.pSample = own.Get();
        }
        DWORD status = 0;
        HRESULT hr = mft_->ProcessOutput(0, 1, &out, &status);
        if (out.pEvents) out.pEvents->Release();
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) break;
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            ComPtr<IMFMediaType> t;
            if (SUCCEEDED(mft_->GetOutputAvailableType(0, 0, &t))) { mft_->SetOutputType(0, t.Get(), 0); outputType_ = t; }
            continue;
        }
        if (FAILED(hr)) { LOG_HR(L"AAC ProcessOutput failed", hr); break; }

        ComPtr<IMFSample> sample;
        if (providesSamples_) sample.Attach(out.pSample); else sample = own;
        if (!sample) break;

        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
        BYTE* data = nullptr;
        DWORD len = 0;
        if (FAILED(buffer->Lock(&data, nullptr, &len))) continue;
        EncodedPacket pkt;
        pkt.data = std::make_shared<std::vector<uint8_t>>(data, data + len);
        buffer->Unlock();

        // Timestamps: prefer the MFT's, otherwise derive from the 1024-frame AAC block count.
        constexpr int64_t kAacFrame = 1024;
        if (FAILED(sample->GetSampleTime(&pkt.pts))) pkt.pts = basePts_ + outputFrames_ * kTicksPerSecond / kAudioSampleRate;
        if (FAILED(sample->GetSampleDuration(&pkt.duration)) || pkt.duration <= 0) pkt.duration = kAacFrame * kTicksPerSecond / kAudioSampleRate;
        outputFrames_ += kAacFrame;
        pkt.keyframe = true;
        if (onPacket_ && len > 0) onPacket_(std::move(pkt));
    }
}

} // namespace clipback
