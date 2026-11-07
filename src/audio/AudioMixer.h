// Clock-driven game/microphone mixer with optional independent AAC tracks.
#pragma once
#include "common/Common.h"
#include "audio/WasapiCapture.h"
#include "audio/AacEncoder.h"
#include "buffer/RingBuffer.h"
#include "settings/Settings.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <deque>
#include <atomic>
#include <array>

namespace clipback {
struct AudioConfig { std::wstring outputDeviceId; bool microphoneEnabled = false; std::wstring microphoneDeviceId; AudioMode mode = AudioMode::Mixed; UINT aacBitrateKbps = 160; float microphoneGain = 1.0f; };
class AudioMixer {
public:
    using PacketCallback = std::function<void(AudioTrack, EncodedPacket&&)>;
    AudioMixer() = default; ~AudioMixer(); AudioMixer(const AudioMixer&) = delete; AudioMixer& operator=(const AudioMixer&) = delete;
    bool Start(const AudioConfig& config, int64_t sessionStart, PacketCallback onPacket, std::wstring& error);
    void Stop(); ComPtr<IMFMediaType> OutputType(AudioTrack track) const; bool HasTrack(AudioTrack track) const; bool HasFailed(std::wstring& error) const; bool MicrophoneActive() const { return micActive_; }
private:
    struct SourceQueue { std::mutex mutex; std::deque<float> samples; void Push(const float* data, size_t frames, size_t maxFrames); size_t Pop(float* out, size_t frames); };
    void ThreadMain(); bool InitializeTrack(AudioTrack track, UINT bitrate, PacketCallback callback, std::wstring& error);
    WasapiCapture game_, mic_; std::array<std::unique_ptr<AacEncoder>, kAudioTrackCount> encoders_; std::array<bool, kAudioTrackCount> enabledTracks_{}; SourceQueue gameQueue_, micQueue_;
    bool gameActive_ = false, micActive_ = false; float micGain_ = 1.0f; int64_t sessionStart_ = 0, producedFrames_ = 0;
    std::thread thread_; std::mutex mutex_; std::condition_variable cv_; bool stop_ = false; std::wstring failure_; mutable std::mutex errorMutex_;
};
} // namespace clipback
