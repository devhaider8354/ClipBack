// Orchestrates capture -> GPU convert -> hardware encode -> ring buffer, plus audio and saving.
//
// Threads while running:
//   * capture thread   (WGC thread-pool callback or duplication thread): convert + hand texture to encoder
//   * encoder thread   : waits on MFT events / queue, pushes packets to the ring buffer
//   * audio threads    : one per WASAPI source (event driven) + mixer (10 ms tick)
//   * watchdog thread  : 1 s tick, surfaces component failures
//   * save thread      : created per save, writes the MP4 without touching the live pipeline
#pragma once
#include "common/Common.h"
#include "settings/Settings.h"
#include "system/SystemInfo.h"
#include "capture/D3DDevice.h"
#include "capture/ICaptureSource.h"
#include "encoder/VideoConverter.h"
#include "encoder/VideoEncoder.h"
#include "audio/AudioMixer.h"
#include "buffer/RingBuffer.h"
#include "muxer/Mp4Writer.h"

#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>

namespace clipback {

enum class EngineEventType { Started, Stopped, Error, SaveStarted, SaveCompleted, SaveFailed, MatchStarted, MatchSaved, MatchFailed };

struct EngineEvent {
    EngineEventType type;
    std::wstring message;   // human readable
    std::wstring path;      // saved file for SaveCompleted
    bool fatal = false;      // true = the pipeline stopped and must be restarted
    double seconds = 0.0;    // SaveStarted / SaveCompleted: replay length
    uint64_t bytes = 0;      // SaveCompleted: file size
};

struct EngineStatus {
    bool running = false;
    bool saving = false;
    std::wstring captureName;
    std::wstring encoderName;
    bool hardwareEncoder = false;
    UINT width = 0, height = 0, fps = 0, bitrateKbps = 0;
    int replayDurationSec = 0;
    double bufferedSeconds = 0.0;
    size_t bufferBytes = 0;
    uint64_t framesEncoded = 0;
    uint64_t framesDropped = 0;
    bool audioActive = false;
    bool microphoneActive = false;
    std::wstring lastSavedFile;
    // Continuous (whole match) recording to disk
    bool matchRecording = false;
    double matchSeconds = 0.0;
    uint64_t matchBytes = 0;
    std::wstring matchFile;
};

class ReplayEngine {
public:
    using EventCallback = std::function<void(const EngineEvent&)>;

    ReplayEngine();
    ~ReplayEngine();
    ReplayEngine(const ReplayEngine&) = delete;
    ReplayEngine& operator=(const ReplayEngine&) = delete;

    void SetEventCallback(EventCallback cb);

    bool Start(const Settings& settings, std::wstring& error);
    void Stop();
    bool IsRunning() const { return running_; }

    // Snapshots the buffer and writes the MP4 on a background thread. Returns false if nothing to save.
    bool SaveReplay(std::wstring& error);

    EngineStatus Status() const;

    // Continuous recording of the whole match straight to disk (runs next to the replay buffer, same encoder,
    // no extra GPU work). Stop finalizes the MP4. Stopping the engine also stops and saves it.
    bool StartMatchRecording(std::wstring& error);
    void StopMatchRecording();
    bool IsMatchRecording() const;

private:
    void OnFrame(const CapturedFrame& frame);
    void OnVideoPacket(EncodedPacket&& pkt);
    void OnAudioPacket(EncodedPacket&& pkt);
    void WatchdogMain();
    void SaveThreadMain(ReplaySnapshot snapshot, ComPtr<IMFMediaType> videoType, ComPtr<IMFMediaType> audioType, std::wstring directory);
    void Emit(const EngineEvent& e);
    void TeardownLocked();
    static std::wstring MakeFileName();
    static std::wstring MakeMatchFileName();
    Mp4WriteResult FinishMatch(const std::shared_ptr<Mp4StreamWriter>& writer);   // never called with matchMutex_ held

    mutable std::mutex stateMutex_;
    EventCallback eventCallback_;
    std::mutex callbackMutex_;

    Settings settings_;
    std::atomic<bool> running_{ false };
    int64_t sessionStart_ = 0;

    std::unique_ptr<D3DDevice> device_;
    std::unique_ptr<ICaptureSource> capture_;
    std::unique_ptr<VideoConverter> converter_;
    std::unique_ptr<VideoEncoder> encoder_;
    std::unique_ptr<AudioMixer> audio_;
    RingBuffer buffer_;
    bool audioActive_ = false;

    // Frame pacing (capture thread only)
    int64_t frameInterval_ = 0;
    int64_t lastAcceptedTs_ = -1;
    std::atomic<uint64_t> convertErrors_{ 0 };

    // Watchdog
    std::thread watchdog_;
    std::mutex watchdogMutex_;
    std::condition_variable watchdogCv_;
    bool watchdogStop_ = false;

    // Saving
    std::thread saveThread_;
    std::atomic<bool> saving_{ false };
    std::wstring lastSavedFile_;

    // Match recording (packets are pushed from the encoder / audio threads under matchMutex_)
    mutable std::mutex matchMutex_;
    std::shared_ptr<Mp4StreamWriter> match_;
    std::wstring matchDirectory_;
};

} // namespace clipback
