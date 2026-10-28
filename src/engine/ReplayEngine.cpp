#include "engine/ReplayEngine.h"
#include "capture/WgcCapture.h"
#include "capture/DuplicationCapture.h"
#include "muxer/Mp4Writer.h"
#include "system/Paths.h"
#include "logging/Logger.h"
#include "common/StringUtils.h"
#include <chrono>

namespace clipback {

ReplayEngine::ReplayEngine() = default;

ReplayEngine::~ReplayEngine()
{
    Stop();
    if (saveThread_.joinable()) saveThread_.join();
}

void ReplayEngine::SetEventCallback(EventCallback cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    eventCallback_ = std::move(cb);
}

void ReplayEngine::Emit(const EngineEvent& e)
{
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        cb = eventCallback_;
    }
    if (cb) cb(e);
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

bool ReplayEngine::Start(const Settings& settings, std::wstring& error)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (running_) return true;
    settings_ = settings;

    // Monitor selection
    std::vector<MonitorInfo> monitors = EnumerateMonitors();
    if (monitors.empty()) { error = L"No display found"; return false; }
    const MonitorInfo& mon = monitors[static_cast<size_t>(settings.monitorIndex) < monitors.size() ? static_cast<size_t>(settings.monitorIndex) : 0];

    // Output resolution
    UINT outW = static_cast<UINT>(mon.width), outH = static_cast<UINT>(mon.height);
    if (settings.resolution == ResolutionMode::R1080p) { outW = 1920; outH = 1080; }
    else if (settings.resolution == ResolutionMode::R720p) { outW = 1280; outH = 720; }
    if (settings.resolution != ResolutionMode::SameAsGame && mon.width > 0 && mon.height > 0) {
        // Keep the monitor aspect ratio (ultrawide etc.) at the requested height.
        const double aspect = static_cast<double>(mon.width) / static_cast<double>(mon.height);
        outW = static_cast<UINT>(outH * aspect + 0.5);
    }
    outW &= ~1u;
    outH &= ~1u;
    if (outW < 64 || outH < 64) { error = L"Invalid capture resolution"; return false; }

    LOG_INFO(L"Starting replay: monitor '%s' %dx%d -> %ux%u @ %d fps, %d kbps, %d s buffer",
        mon.deviceName.c_str(), mon.width, mon.height, outW, outH, settings.fps, settings.bitrateKbps, settings.replayDurationSec);

    // 1) Direct3D device on the adapter driving that monitor
    device_ = std::make_unique<D3DDevice>();
    if (!device_->Create(mon.handle, error)) { TeardownLocked(); return false; }

    GpuVendor vendor = GpuVendor::Unknown;
    if (device_->Adapter()) {
        DXGI_ADAPTER_DESC1 desc{};
        device_->Adapter()->GetDesc1(&desc);
        vendor = VendorFromId(desc.VendorId);
    }

    // 2) GPU colour converter
    converter_ = std::make_unique<VideoConverter>();
    if (!converter_->Initialize(*device_, outW, outH, static_cast<UINT>(settings.fps), 8, error)) { TeardownLocked(); return false; }

    // 3) Ring buffer + session clock
    buffer_.Clear();
    buffer_.SetRetention(static_cast<int64_t>(settings.replayDurationSec) * kTicksPerSecond);
    sessionStart_ = Now100ns();
    frameInterval_ = kTicksPerSecond / settings.fps;
    lastAcceptedTs_ = -1;
    convertErrors_ = 0;

    // 4) Encoder
    encoder_ = std::make_unique<VideoEncoder>();
    VideoEncoderConfig ec;
    ec.width = outW;
    ec.height = outH;
    ec.fps = static_cast<UINT>(settings.fps);
    ec.bitrateKbps = static_cast<UINT>(settings.bitrateKbps);
    ec.preference = settings.encoder;
    ec.preferredVendor = vendor;
    if (!encoder_->Initialize(*device_, ec, [this](EncodedPacket&& p) { OnVideoPacket(std::move(p)); }, error)) { TeardownLocked(); return false; }

    // 5) Audio (failure here is reported but does not abort - video-only replay is better than none)
    audio_ = std::make_unique<AudioMixer>();
    AudioConfig ac;
    ac.outputDeviceId = settings.outputDeviceId;
    ac.microphoneEnabled = settings.microphoneEnabled;
    ac.microphoneDeviceId = settings.microphoneDeviceId;
    ac.aacBitrateKbps = 160;
    std::wstring audioError;
    audioActive_ = audio_->Start(ac, sessionStart_, [this](EncodedPacket&& p) { OnAudioPacket(std::move(p)); }, audioError);
    if (!audioActive_) {
        LOG_ERROR(L"Audio capture unavailable: %s", audioError.c_str());
        audio_.reset();
        Emit({ EngineEventType::Error, L"Audio capture unavailable - recording video only. " + audioError, L"", false });
    }

    // 6) Capture: WGC first, Desktop Duplication as fallback
    FrameCallback cb = [this](const CapturedFrame& f) { OnFrame(f); };
    std::wstring wgcError;
    if (WgcCapture::IsSupported()) {
        auto wgc = std::make_unique<WgcCapture>();
        if (wgc->Start(*device_, mon.handle, settings.captureCursor, cb, wgcError)) {
            capture_ = std::move(wgc);
        } else {
            LOG_WARN(L"WGC unavailable (%s) - trying Desktop Duplication", wgcError.c_str());
        }
    } else {
        wgcError = L"Windows Graphics Capture requires Windows 10 version 1903 or newer";
        LOG_WARN(L"%s", wgcError.c_str());
    }
    if (!capture_) {
        auto dup = std::make_unique<DuplicationCapture>();
        std::wstring dupError;
        if (dup->Start(*device_, mon.handle, settings.captureCursor, cb, dupError)) {
            capture_ = std::move(dup);
        } else {
            error = L"Screen capture failed. " + wgcError + L" / " + dupError;
            TeardownLocked();
            return false;
        }
    }

    // 7) Watchdog
    watchdogStop_ = false;
    watchdog_ = std::thread(&ReplayEngine::WatchdogMain, this);

    running_ = true;
    Emit({ EngineEventType::Started, L"Replay buffering started", L"", false });
    return true;
}

void ReplayEngine::TeardownLocked()
{
    // Order matters: stop producers before consumers.
    if (capture_) { capture_->Stop(); capture_.reset(); }
    if (audio_) { audio_->Stop(); audio_.reset(); }
    if (encoder_) { encoder_->Shutdown(); encoder_.reset(); }
    if (converter_) { converter_->Shutdown(); converter_.reset(); }
    if (device_) { device_->Destroy(); device_.reset(); }
    audioActive_ = false;
}

void ReplayEngine::Stop()
{
    {
        std::lock_guard<std::mutex> lock(watchdogMutex_);
        watchdogStop_ = true;
    }
    watchdogCv_.notify_all();
    if (watchdog_.joinable()) watchdog_.join();

    std::lock_guard<std::mutex> lock(stateMutex_);
    const bool wasRunning = running_.exchange(false);
    TeardownLocked();   // no more packets after this
    std::shared_ptr<Mp4StreamWriter> match;
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        match = std::move(match_);
    }
    if (match) {
        const Mp4WriteResult r = FinishMatch(match);
        if (r.ok) lastSavedFile_ = r.path;
    }
    buffer_.Clear();
    if (wasRunning) {
        LOG_INFO(L"Replay stopped");
        Emit({ EngineEventType::Stopped, L"Replay stopped", L"", false });
    }
}

// ---------------------------------------------------------------------------
// Pipeline callbacks (worker threads)
// ---------------------------------------------------------------------------

void ReplayEngine::OnFrame(const CapturedFrame& frame)
{
    if (!encoder_ || !converter_) return;   // capture is always stopped before these are destroyed
    const int64_t ts = frame.timestamp100ns - sessionStart_;
    if (ts < 0) return;

    // Frame pacing: accept at most `fps` frames per second (capture may run at the monitor refresh rate).
    if (lastAcceptedTs_ >= 0 && ts - lastAcceptedTs_ < frameInterval_ - frameInterval_ / 8) return;
    lastAcceptedTs_ = ts;

    std::wstring err;
    ID3D11Texture2D* nv12 = converter_->Convert(frame.texture, frame.width, frame.height, err);
    if (!nv12) {
        if (convertErrors_++ % 300 == 0) LOG_ERROR(L"Frame conversion failed: %s", err.c_str());
        return;
    }
    encoder_->Submit(nv12, ts, frameInterval_);
}

void ReplayEngine::OnVideoPacket(EncodedPacket&& pkt)
{
    {
        std::lock_guard<std::mutex> lock(matchMutex_);
        if (match_) match_->PushVideo(pkt);
    }
    buffer_.PushVideo(std::move(pkt));
}

void ReplayEngine::OnAudioPacket(EncodedPacket&& pkt)
{
    {
        std::lock_guard<std::mutex> lock(matchMutex_);
        if (match_) match_->PushAudio(pkt);
    }
    buffer_.PushAudio(std::move(pkt));
}

void ReplayEngine::WatchdogMain()
{
    ComInitializer com;
    std::unique_lock<std::mutex> lock(watchdogMutex_);
    while (!watchdogStop_) {
        watchdogCv_.wait_for(lock, std::chrono::seconds(1), [&] { return watchdogStop_; });
        if (watchdogStop_) break;
        lock.unlock();

        std::wstring err;
        bool fatal = false;
        std::shared_ptr<Mp4StreamWriter> brokenMatch;
        {
            std::unique_lock<std::mutex> state(stateMutex_, std::try_to_lock);
            if (state.owns_lock() && running_) {
                {
                    // Match recording: stop cleanly on a disk write error or when the disk is almost full.
                    std::lock_guard<std::mutex> m(matchMutex_);
                    if (match_) {
                        ULARGE_INTEGER freeBytes{};
                        const bool lowDisk = GetDiskFreeSpaceExW(matchDirectory_.c_str(), &freeBytes, nullptr, nullptr) &&
                            freeBytes.QuadPart < 300ull * 1024 * 1024;
                        if (lowDisk) LOG_WARN(L"Match recording: disk almost full - stopping");
                        if (lowDisk || match_->HasFailed()) brokenMatch = std::move(match_);
                    }
                }
                if (capture_ && capture_->HasFailed(err)) { err = L"Capture stopped: " + err; fatal = true; }
                else if (encoder_ && encoder_->HasFailed(err)) { err = L"Encoder error: " + err; fatal = true; }
                else if (audio_ && audio_->HasFailed(err)) {
                    LOG_ERROR(L"Audio failed, continuing video-only: %s", err.c_str());
                    audio_->Stop();
                    audio_.reset();
                    audioActive_ = false;
                    Emit({ EngineEventType::Error, L"Audio stopped: " + err + L" (recording continues without audio)", L"", false });
                }
            }
        }
        if (brokenMatch) {
            const Mp4WriteResult r = FinishMatch(brokenMatch);
            if (r.ok) {
                std::lock_guard<std::mutex> state(stateMutex_);
                lastSavedFile_ = r.path;
            }
        }
        if (fatal) {
            LOG_ERROR(L"%s", err.c_str());
            // The UI owns the lifecycle: it calls Stop()/Start() on its own thread after this event.
            Emit({ EngineEventType::Error, err, L"", true });
            lock.lock();
            // Avoid emitting the same failure every second.
            watchdogCv_.wait(lock, [&] { return watchdogStop_; });
            break;
        }
        lock.lock();
    }
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

std::wstring ReplayEngine::MakeFileName()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return str::Format(L"ClipBack_%04u-%02u-%02u_%02u-%02u-%02u.mp4", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::wstring ReplayEngine::MakeMatchFileName()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return str::Format(L"ClipBack_Match_%04u-%02u-%02u_%02u-%02u-%02u.mp4", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

bool ReplayEngine::StartMatchRecording(std::wstring& error)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || !encoder_) { error = L"Replay is not running"; return false; }
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        if (match_) return true;   // already recording
    }
    ComPtr<IMFMediaType> videoType = encoder_->OutputType();
    if (!videoType) { error = L"Nothing recorded yet - wait a second and try again"; return false; }
    ComPtr<IMFMediaType> audioType = audio_ ? audio_->OutputType() : nullptr;

    const std::wstring directory = settings_.EffectiveSaveDirectory();
    if (!paths::EnsureDirectory(directory)) { error = L"Cannot create folder: " + directory; return false; }
    ULARGE_INTEGER freeBytes{};
    if (GetDiskFreeSpaceExW(directory.c_str(), &freeBytes, nullptr, nullptr) && freeBytes.QuadPart < 2ull * 1024 * 1024 * 1024) {
        error = L"Not enough free disk space (need at least 2 GB) in " + directory;
        return false;
    }

    auto writer = std::make_shared<Mp4StreamWriter>();
    const std::wstring path = paths::Join(directory, MakeMatchFileName());
    if (!writer->Open(path, videoType.Get(), audioType.Get(), error)) {
        LOG_ERROR(L"Match recording could not start: %s", error.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        match_ = writer;
        matchDirectory_ = directory;
    }
    Emit({ EngineEventType::MatchStarted, L"Match recording started", path, false });
    return true;
}

void ReplayEngine::StopMatchRecording()
{
    std::shared_ptr<Mp4StreamWriter> match;
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        match = std::move(match_);
    }
    if (!match) return;
    const Mp4WriteResult r = FinishMatch(match);
    if (r.ok) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastSavedFile_ = r.path;
    }
}

bool ReplayEngine::IsMatchRecording() const
{
    std::lock_guard<std::mutex> m(matchMutex_);
    return match_ != nullptr;
}

Mp4WriteResult ReplayEngine::FinishMatch(const std::shared_ptr<Mp4StreamWriter>& writer)
{
    const Mp4WriteResult r = writer->Close();
    if (r.ok) {
        Emit({ EngineEventType::MatchSaved, str::Format(L"Match saved: %.0f min (%s)", r.durationSeconds / 60.0, str::BytesToHuman(r.bytes).c_str()), r.path, false, r.durationSeconds, r.bytes });
    } else {
        Emit({ EngineEventType::MatchFailed, r.error, L"", false });
    }
    return r;
}

bool ReplayEngine::SaveReplay(std::wstring& error)
{
    ReplaySnapshot snapshot;
    ComPtr<IMFMediaType> videoType, audioType;
    std::wstring directory;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!running_ || !encoder_) { error = L"Replay is not running"; return false; }
        if (saving_) { error = L"A replay is already being saved"; return false; }
        videoType = encoder_->OutputType();
        if (!videoType) { error = L"Nothing recorded yet - wait a second and try again"; return false; }
        if (!buffer_.Snapshot(snapshot)) { error = L"Nothing recorded yet - wait a second and try again"; return false; }
        if (audio_) audioType = audio_->OutputType();
        directory = settings_.EffectiveSaveDirectory();
        saving_ = true;
    }
    if (saveThread_.joinable()) saveThread_.join();   // previous save finished (flag was false) - reclaim the thread
    saveThread_ = std::thread(&ReplayEngine::SaveThreadMain, this, std::move(snapshot), videoType, audioType, directory);
    return true;
}

void ReplayEngine::SaveThreadMain(ReplaySnapshot snapshot, ComPtr<IMFMediaType> videoType, ComPtr<IMFMediaType> audioType, std::wstring directory)
{
    ComInitializer com;
    // Saving must never steal time from the game: lowest priority is fine because the data is already in RAM.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    const double seconds = static_cast<double>(snapshot.DurationTicks()) / kTicksPerSecond;
    Emit({ EngineEventType::SaveStarted, str::Format(L"Saving last %.0f seconds...", seconds), L"", false, seconds, 0 });

    if (!paths::EnsureDirectory(directory)) {
        saving_ = false;
        Emit({ EngineEventType::SaveFailed, L"Cannot create folder: " + directory, L"", false });
        return;
    }
    const std::wstring path = paths::Join(directory, MakeFileName());
    const auto t0 = std::chrono::steady_clock::now();
    Mp4WriteResult r = Mp4Writer::Write(path, snapshot, videoType.Get(), audioType.Get());
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (r.ok) {
        LOG_INFO(L"Saved %s (%.1f s, %s, %zu video / %zu audio packets) in %.2f s", path.c_str(), r.durationSeconds,
            str::BytesToHuman(r.bytes).c_str(), snapshot.video.size(), snapshot.audio.size(), elapsed);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastSavedFile_ = path;
        }
        saving_ = false;
        Emit({ EngineEventType::SaveCompleted, str::Format(L"Saved %.0f s replay (%s)", r.durationSeconds, str::BytesToHuman(r.bytes).c_str()), path, false, r.durationSeconds, r.bytes });
    } else {
        LOG_ERROR(L"Save failed: %s", r.error.c_str());
        DeleteFileW(path.c_str());
        saving_ = false;
        Emit({ EngineEventType::SaveFailed, r.error, L"", false });
    }
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

EngineStatus ReplayEngine::Status() const
{
    EngineStatus s;
    std::unique_lock<std::mutex> lock(stateMutex_, std::try_to_lock);
    s.running = running_;
    s.saving = saving_;
    s.replayDurationSec = settings_.replayDurationSec;
    s.fps = static_cast<UINT>(settings_.fps);
    s.bitrateKbps = static_cast<UINT>(settings_.bitrateKbps);
    if (!lock.owns_lock()) return s;   // engine is starting/stopping; return the basics
    s.lastSavedFile = lastSavedFile_;
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        if (match_) {
            s.matchRecording = true;
            s.matchSeconds = match_->Seconds();
            s.matchBytes = match_->Bytes();
            s.matchFile = match_->Path();
        }
    }
    if (!running_) return s;
    if (capture_) s.captureName = capture_->Name();
    if (encoder_) {
        s.encoderName = encoder_->SelectedEncoder().friendlyName;
        s.hardwareEncoder = encoder_->IsHardware();
        const VideoEncoderStats es = encoder_->Stats();
        s.framesEncoded = es.framesEncoded;
        s.framesDropped = es.framesDropped;
    }
    if (converter_) { s.width = converter_->OutputWidth(); s.height = converter_->OutputHeight(); }
    const RingBufferStats bs = buffer_.Stats();
    s.bufferedSeconds = bs.bufferedSeconds;
    s.bufferBytes = bs.bytes;
    s.audioActive = audioActive_;
    s.microphoneActive = audio_ ? audio_->MicrophoneActive() : false;
    return s;
}

} // namespace clipback
