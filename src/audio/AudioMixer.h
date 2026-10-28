// Mixes game audio + microphone into one 48 kHz stereo stream that is timestamped on the same clock
// as the video frames, then feeds 1024-frame blocks to the AAC encoder.
//
// The mixer is clock-driven: every 10 ms it produces exactly as many frames as wall-clock time has
// elapsed, padding with silence when a source has no data (loopback delivers nothing while the game is
// silent) and dropping backlog when a source drifts ahead. That keeps A/V sync exact without any
// resampling tricks.
#pragma once
#include "common/Common.h"
#include "audio/WasapiCapture.h"
#include "audio/AacEncoder.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <deque>
#include <atomic>

namespace clipback {

struct AudioConfig {
    std::wstring outputDeviceId;     // game audio (loopback) endpoint, empty = default
    bool microphoneEnabled = false;
    std::wstring microphoneDeviceId;
    UINT aacBitrateKbps = 160;
    float microphoneGain = 1.0f;
};

class AudioMixer {
public:
    AudioMixer() = default;
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // sessionStart: Now100ns() value all pts are relative to.
    bool Start(const AudioConfig& config, int64_t sessionStart, std::function<void(EncodedPacket&&)> onPacket, std::wstring& error);
    void Stop();
    ComPtr<IMFMediaType> OutputType() const { return aac_.OutputType(); }
    bool HasFailed(std::wstring& error) const;
    bool MicrophoneActive() const { return micActive_; }

private:
    struct SourceQueue {
        std::mutex mutex;
        std::deque<float> samples;   // interleaved stereo
        void Push(const float* data, size_t frames, size_t maxFrames);
        size_t Pop(float* out, size_t frames);   // returns frames actually popped (rest must be zero-filled)
    };

    void ThreadMain();

    WasapiCapture game_;
    WasapiCapture mic_;
    AacEncoder aac_;
    SourceQueue gameQueue_;
    SourceQueue micQueue_;
    bool micActive_ = false;
    float micGain_ = 1.0f;
    int64_t sessionStart_ = 0;
    int64_t producedFrames_ = 0;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    std::wstring failure_;
    mutable std::mutex errorMutex_;
};

} // namespace clipback
