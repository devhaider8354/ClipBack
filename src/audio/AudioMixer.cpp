#include "audio/AudioMixer.h"
#include "logging/Logger.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace clipback {

namespace {
constexpr size_t kAacBlockFrames = 1024;
constexpr size_t kMaxQueuedFrames = kAudioSampleRate / 2;   // 500 ms backlog cap per source (drift control)
constexpr int kTickMs = 10;
}

AudioMixer::~AudioMixer() { Stop(); }

void AudioMixer::SourceQueue::Push(const float* data, size_t frames, size_t maxFrames)
{
    std::lock_guard<std::mutex> lock(mutex);
    samples.insert(samples.end(), data, data + frames * 2);
    const size_t maxSamples = maxFrames * 2;
    if (samples.size() > maxSamples) {
        samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - maxSamples));
    }
}

size_t AudioMixer::SourceQueue::Pop(float* out, size_t frames)
{
    std::lock_guard<std::mutex> lock(mutex);
    const size_t available = samples.size() / 2;
    const size_t n = std::min(available, frames);
    for (size_t i = 0; i < n * 2; ++i) out[i] = samples[i];
    samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(n * 2));
    return n;
}

bool AudioMixer::Start(const AudioConfig& config, int64_t sessionStart, std::function<void(EncodedPacket&&)> onPacket, std::wstring& error)
{
    Stop();
    sessionStart_ = sessionStart;
    producedFrames_ = 0;
    micGain_ = config.microphoneGain;
    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        failure_.clear();
    }

    if (!aac_.Initialize(config.aacBitrateKbps, std::move(onPacket), error)) return false;

    if (!game_.Start(config.outputDeviceId, true, [this](const float* s, size_t n) { gameQueue_.Push(s, n, kMaxQueuedFrames); }, error)) {
        aac_.Shutdown();
        return false;
    }

    micActive_ = false;
    if (config.microphoneEnabled) {
        std::wstring micErr;
        if (mic_.Start(config.microphoneDeviceId, false, [this](const float* s, size_t n) { micQueue_.Push(s, n, kMaxQueuedFrames); }, micErr)) {
            micActive_ = true;
        } else {
            // A missing microphone must not stop the replay - log and continue with game audio only.
            LOG_WARN(L"Microphone unavailable, continuing without it: %s", micErr.c_str());
        }
    }

    thread_ = std::thread(&AudioMixer::ThreadMain, this);
    return true;
}

void AudioMixer::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    game_.Stop();
    mic_.Stop();
    aac_.Shutdown();
    {
        std::lock_guard<std::mutex> lock(gameQueue_.mutex);
        gameQueue_.samples.clear();
    }
    {
        std::lock_guard<std::mutex> lock(micQueue_.mutex);
        micQueue_.samples.clear();
    }
    micActive_ = false;
}

bool AudioMixer::HasFailed(std::wstring& error) const
{
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        if (!failure_.empty()) { error = failure_; return true; }
    }
    return game_.HasFailed(error);
}

void AudioMixer::ThreadMain()
{
    ComInitializer com;
    std::vector<float> gameBuf(kAacBlockFrames * 2), micBuf(kAacBlockFrames * 2);
    std::vector<int16_t> pcm(kAacBlockFrames * 2);

    // Prime: let the first tick line up with the session clock.
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        // Timed wait = kernel sleep, no polling loop.
        cv_.wait_for(lock, std::chrono::milliseconds(kTickMs), [&] { return stop_; });
        if (stop_) break;
        lock.unlock();

        const int64_t elapsed = Now100ns() - sessionStart_;
        const int64_t targetFrames = elapsed * kAudioSampleRate / kTicksPerSecond;

        // Emit as many whole AAC blocks as the clock allows (normally 0 or 1 per tick).
        int blocks = 0;
        while (producedFrames_ + static_cast<int64_t>(kAacBlockFrames) <= targetFrames && blocks < 8) {
            const size_t g = gameQueue_.Pop(gameBuf.data(), kAacBlockFrames);
            std::fill(gameBuf.begin() + static_cast<std::ptrdiff_t>(g * 2), gameBuf.end(), 0.0f);

            size_t m = 0;
            if (micActive_) {
                m = micQueue_.Pop(micBuf.data(), kAacBlockFrames);
                std::fill(micBuf.begin() + static_cast<std::ptrdiff_t>(m * 2), micBuf.end(), 0.0f);
            }

            for (size_t i = 0; i < kAacBlockFrames * 2; ++i) {
                float v = gameBuf[i];
                if (micActive_) v += micBuf[i] * micGain_;
                v = std::max(-1.0f, std::min(1.0f, v));
                pcm[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
            }

            const int64_t pts = producedFrames_ * kTicksPerSecond / kAudioSampleRate;
            aac_.Encode(pcm.data(), kAacBlockFrames, pts);
            producedFrames_ += static_cast<int64_t>(kAacBlockFrames);
            ++blocks;
        }

        // If we fell far behind (system sleep), jump forward instead of emitting minutes of silence.
        if (targetFrames - producedFrames_ > static_cast<int64_t>(kAudioSampleRate) * 2) {
            LOG_WARN(L"Audio clock jumped %.1f s - resyncing", static_cast<double>(targetFrames - producedFrames_) / kAudioSampleRate);
            producedFrames_ = targetFrames - static_cast<int64_t>(targetFrames % kAacBlockFrames);
        }

        lock.lock();
    }
}

} // namespace clipback
