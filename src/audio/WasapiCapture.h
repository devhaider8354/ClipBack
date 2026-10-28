// WASAPI shared-mode capture: game audio via loopback on a render endpoint, or a microphone.
// Output is always 48 kHz / stereo / float32 so the mixer never has to convert.
#pragma once
#include "common/Common.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace clipback {

constexpr UINT kAudioSampleRate = 48000;
constexpr UINT kAudioChannels = 2;

// interleaved stereo float frames
using AudioFrameCallback = std::function<void(const float* samples, size_t frames)>;

class WasapiCapture {
public:
    WasapiCapture() = default;
    ~WasapiCapture();
    WasapiCapture(const WasapiCapture&) = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    // deviceId empty = default endpoint. loopback=true captures what is being played on a render device.
    bool Start(const std::wstring& deviceId, bool loopback, AudioFrameCallback callback, std::wstring& error);
    void Stop();
    bool HasFailed(std::wstring& error) const;
    std::wstring DeviceName() const { return deviceName_; }

private:
    void ThreadMain();
    void ConvertAndDeliver(const BYTE* data, UINT32 frames, bool silent);

    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    UniqueHandle event_;
    WAVEFORMATEXTENSIBLE format_{};   // actual stream format
    bool nativeFormat_ = false;       // true when the stream is already 48k/stereo/float (no conversion)
    bool isFloat_ = false;
    UINT bitsPerSample_ = 32;
    UINT channels_ = 2;
    UINT sampleRate_ = 48000;
    double resamplePos_ = 0.0;
    std::vector<float> convertScratch_;
    std::vector<float> outScratch_;
    float lastFrame_[2] = { 0.0f, 0.0f };

    AudioFrameCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    bool loopback_ = false;
    std::wstring deviceName_;
    mutable std::mutex errorMutex_;
    std::wstring failure_;
};

} // namespace clipback
