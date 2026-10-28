#include "audio/WasapiCapture.h"
#include "logging/Logger.h"
#include <propkeydef.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")

namespace clipback {

// PKEY_Device_FriendlyName without relying on INITGUID being defined in this translation unit.
static const PROPERTYKEY kPKEY_Device_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

WasapiCapture::~WasapiCapture() { Stop(); }

static ComPtr<IMMDevice> OpenDevice(const std::wstring& id, bool loopback, std::wstring& name, std::wstring& error)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) { error = L"MMDeviceEnumerator failed: " + logging::HResultToString(hr); return nullptr; }

    ComPtr<IMMDevice> device;
    if (!id.empty()) {
        hr = enumerator->GetDevice(id.c_str(), &device);
        if (FAILED(hr)) LOG_WARN(L"Configured audio device not found (%s), using default", logging::HResultToString(hr).c_str());
    }
    if (!device) {
        hr = enumerator->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device);
        if (FAILED(hr)) { error = std::wstring(loopback ? L"No default audio output device: " : L"No default microphone: ") + logging::HResultToString(hr); return nullptr; }
    }
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(props->GetValue(kPKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) name = pv.pwszVal;
        PropVariantClear(&pv);
    }
    return device;
}

bool WasapiCapture::Start(const std::wstring& deviceId, bool loopback, AudioFrameCallback callback, std::wstring& error)
{
    Stop();
    callback_ = std::move(callback);
    loopback_ = loopback;
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        failure_.clear();
    }

    ComPtr<IMMDevice> device = OpenDevice(deviceId, loopback, deviceName_, error);
    if (!device) return false;

    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_);
    if (FAILED(hr)) { error = L"IAudioClient activation failed: " + logging::HResultToString(hr); return false; }

    // Ask WASAPI for 48k/stereo/float directly. Windows 10+ converts on the fly (AUTOCONVERTPCM).
    WAVEFORMATEXTENSIBLE wanted{};
    wanted.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wanted.Format.nChannels = static_cast<WORD>(kAudioChannels);
    wanted.Format.nSamplesPerSec = kAudioSampleRate;
    wanted.Format.wBitsPerSample = 32;
    wanted.Format.nBlockAlign = static_cast<WORD>(wanted.Format.nChannels * wanted.Format.wBitsPerSample / 8);
    wanted.Format.nAvgBytesPerSec = wanted.Format.nSamplesPerSec * wanted.Format.nBlockAlign;
    wanted.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wanted.Samples.wValidBitsPerSample = 32;
    wanted.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    wanted.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    const REFERENCE_TIME bufferDuration = 40 * 10000;   // 40 ms buffer
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

    hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration, 0, &wanted.Format, nullptr);
    if (SUCCEEDED(hr)) {
        format_ = wanted;
        nativeFormat_ = true;
    } else {
        // Fall back to the device mix format and convert ourselves.
        LOG_INFO(L"WASAPI auto-convert not accepted (%s) - using mix format with internal conversion", logging::HResultToString(hr).c_str());
        client_.Reset();
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client_);
        if (FAILED(hr)) { error = L"IAudioClient re-activation failed: " + logging::HResultToString(hr); return false; }
        WAVEFORMATEX* mix = nullptr;
        hr = client_->GetMixFormat(&mix);
        if (FAILED(hr) || !mix) { error = L"GetMixFormat failed: " + logging::HResultToString(hr); return false; }
        memset(&format_, 0, sizeof(format_));
        memcpy(&format_, mix, std::min<size_t>(sizeof(WAVEFORMATEX) + mix->cbSize, sizeof(format_)));
        flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, bufferDuration, 0, mix, nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr)) { error = L"IAudioClient::Initialize failed: " + logging::HResultToString(hr); return false; }
        nativeFormat_ = false;
    }

    channels_ = format_.Format.nChannels;
    sampleRate_ = format_.Format.nSamplesPerSec;
    bitsPerSample_ = format_.Format.wBitsPerSample;
    isFloat_ = (format_.Format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
               (format_.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && format_.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    if (nativeFormat_) { channels_ = kAudioChannels; sampleRate_ = kAudioSampleRate; isFloat_ = true; bitsPerSample_ = 32; }

    event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event_) { error = L"CreateEvent failed"; return false; }
    hr = client_->SetEventHandle(event_.get());
    if (FAILED(hr)) { error = L"SetEventHandle failed: " + logging::HResultToString(hr); return false; }

    hr = client_->GetService(__uuidof(IAudioCaptureClient), &capture_);
    if (FAILED(hr)) { error = L"IAudioCaptureClient unavailable: " + logging::HResultToString(hr); return false; }

    hr = client_->Start();
    if (FAILED(hr)) { error = L"IAudioClient::Start failed: " + logging::HResultToString(hr); return false; }

    resamplePos_ = 0.0;
    lastFrame_[0] = lastFrame_[1] = 0.0f;
    running_ = true;
    thread_ = std::thread(&WasapiCapture::ThreadMain, this);
    LOG_INFO(L"Audio %s started on '%s' (%u Hz, %u ch, %s%s)", loopback ? L"loopback" : L"microphone", deviceName_.c_str(),
        sampleRate_, channels_, isFloat_ ? L"float" : L"pcm", nativeFormat_ ? L", native 48k" : L", converting");
    return true;
}

void WasapiCapture::Stop()
{
    running_ = false;
    if (event_) SetEvent(event_.get());
    if (thread_.joinable()) thread_.join();
    if (client_) client_->Stop();
    capture_.Reset();
    client_.Reset();
    event_.reset();
    callback_ = nullptr;
}

bool WasapiCapture::HasFailed(std::wstring& error) const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    error = failure_;
    return !failure_.empty();
}

// Converts any PCM/float layout to 48k stereo float and delivers it.
void WasapiCapture::ConvertAndDeliver(const BYTE* data, UINT32 frames, bool silent)
{
    if (frames == 0) return;

    if (nativeFormat_) {
        if (silent) {
            outScratch_.assign(static_cast<size_t>(frames) * 2, 0.0f);
            callback_(outScratch_.data(), frames);
        } else {
            callback_(reinterpret_cast<const float*>(data), frames);
        }
        return;
    }

    // 1) to stereo float at the device rate
    convertScratch_.resize(static_cast<size_t>(frames) * 2);
    if (silent) {
        std::fill(convertScratch_.begin(), convertScratch_.end(), 0.0f);
    } else {
        const UINT bytesPerSample = bitsPerSample_ / 8;
        for (UINT32 f = 0; f < frames; ++f) {
            float l = 0.0f, r = 0.0f;
            for (UINT c = 0; c < channels_; ++c) {
                const BYTE* p = data + (static_cast<size_t>(f) * channels_ + c) * bytesPerSample;
                float v;
                if (isFloat_) v = *reinterpret_cast<const float*>(p);
                else if (bitsPerSample_ == 16) v = *reinterpret_cast<const int16_t*>(p) / 32768.0f;
                else if (bitsPerSample_ == 32) v = *reinterpret_cast<const int32_t*>(p) / 2147483648.0f;
                else if (bitsPerSample_ == 24) { const int32_t s = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); v = s / 2147483648.0f; }
                else if (bitsPerSample_ == 8) v = (p[0] - 128) / 128.0f;
                else v = 0.0f;
                if (channels_ == 1) { l += v; r += v; }
                else if (c == 0) l += v;
                else if (c == 1) r += v;
                else if (c == 2) { l += v * 0.7f; r += v * 0.7f; }   // centre
                else if (c == 4 || c == 6) l += v * 0.5f;             // surround left
                else if (c == 5 || c == 7) r += v * 0.5f;             // surround right
            }
            convertScratch_[static_cast<size_t>(f) * 2] = l;
            convertScratch_[static_cast<size_t>(f) * 2 + 1] = r;
        }
    }

    // 2) resample to 48 kHz (linear interpolation - adequate for game/mic audio and near-free on the CPU)
    if (sampleRate_ == kAudioSampleRate) {
        callback_(convertScratch_.data(), frames);
        return;
    }
    const double step = static_cast<double>(sampleRate_) / static_cast<double>(kAudioSampleRate);
    outScratch_.clear();
    outScratch_.reserve(static_cast<size_t>(frames / step) + 4);
    double pos = resamplePos_;
    while (pos < static_cast<double>(frames)) {
        const long i = static_cast<long>(std::floor(pos));
        const float t = static_cast<float>(pos - i);
        float l0, r0;
        if (i < 0) { l0 = lastFrame_[0]; r0 = lastFrame_[1]; }
        else { l0 = convertScratch_[static_cast<size_t>(i) * 2]; r0 = convertScratch_[static_cast<size_t>(i) * 2 + 1]; }
        float l1, r1;
        if (i + 1 < static_cast<long>(frames)) { l1 = convertScratch_[static_cast<size_t>(i + 1) * 2]; r1 = convertScratch_[static_cast<size_t>(i + 1) * 2 + 1]; }
        else { l1 = l0; r1 = r0; }
        outScratch_.push_back(l0 + (l1 - l0) * t);
        outScratch_.push_back(r0 + (r1 - r0) * t);
        pos += step;
    }
    resamplePos_ = pos - static_cast<double>(frames);
    lastFrame_[0] = convertScratch_[static_cast<size_t>(frames - 1) * 2];
    lastFrame_[1] = convertScratch_[static_cast<size_t>(frames - 1) * 2 + 1];
    if (!outScratch_.empty()) callback_(outScratch_.data(), outScratch_.size() / 2);
}

void WasapiCapture::ThreadMain()
{
    ComInitializer com;
    DWORD taskIndex = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);

    int errorStreak = 0;
    while (running_) {
        // Loopback streams only signal the event while something is rendering; a 20 ms timeout keeps
        // the loop alive for silence without spinning (kernel wait, ~50 wakeups/s at most).
        WaitForSingleObject(event_.get(), 20);
        if (!running_) break;

        for (;;) {
            UINT32 packetFrames = 0;
            HRESULT hr = capture_->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                    std::lock_guard<std::mutex> lock(errorMutex_);
                    failure_ = L"Audio device was removed";
                    running_ = false;
                }
                if (++errorStreak > 100) { std::lock_guard<std::mutex> lock(errorMutex_); failure_ = L"Audio capture failed: " + logging::HResultToString(hr); running_ = false; }
                break;
            }
            if (packetFrames == 0) break;
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { ++errorStreak; break; }
            errorStreak = 0;
            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            if (callback_) ConvertAndDeliver(data, frames, silent);
            capture_->ReleaseBuffer(frames);
        }
    }

    if (avrt) AvRevertMmThreadCharacteristics(avrt);
}

} // namespace clipback
