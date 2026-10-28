// Writes a ReplaySnapshot (already-encoded H.264 + AAC packets) into an .mp4 using the
// Media Foundation sink writer in pass-through mode (no re-encoding).
#pragma once
#include "common/Common.h"
#include "buffer/RingBuffer.h"
#include <mfidl.h>
#include <string>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace clipback {

struct Mp4WriteResult {
    bool ok = false;
    std::wstring error;
    std::wstring path;
    double durationSeconds = 0.0;
    uint64_t bytes = 0;
};

class Mp4Writer {
public:
    // videoType must carry MF_MT_MPEG_SEQUENCE_HEADER (SPS/PPS). audioType may be null (video-only file).
    static Mp4WriteResult Write(const std::wstring& path, const ReplaySnapshot& snapshot, IMFMediaType* videoType, IMFMediaType* audioType);
};

// Continuous recording straight to disk (a whole match): packets are queued by the pipeline threads and
// written by a low-priority worker thread, so the encoder is never blocked by the disk.
// The file starts on the first keyframe after Open(); Close() finalizes it (playable only after Close).
class Mp4StreamWriter {
public:
    Mp4StreamWriter() = default;
    ~Mp4StreamWriter();
    Mp4StreamWriter(const Mp4StreamWriter&) = delete;
    Mp4StreamWriter& operator=(const Mp4StreamWriter&) = delete;

    bool Open(const std::wstring& path, IMFMediaType* videoType, IMFMediaType* audioType, std::wstring& error);
    void PushVideo(const EncodedPacket& pkt);   // any thread, cheap
    void PushAudio(const EncodedPacket& pkt);   // any thread, cheap
    Mp4WriteResult Close();                      // blocks until the file is finalized

    bool HasFailed() const { return failed_; }
    double Seconds() const { return static_cast<double>(writtenTicks_.load()) / kTicksPerSecond; }
    uint64_t Bytes() const { return writtenBytes_; }
    const std::wstring& Path() const { return path_; }

private:
    struct Item { EncodedPacket pkt; bool audio; };
    void WorkerMain(ComPtr<IMFMediaType> videoType, ComPtr<IMFMediaType> audioType);
    void Push(const EncodedPacket& pkt, bool audio);

    std::wstring path_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    bool stop_ = false;
    bool accepting_ = false;

    std::mutex openMutex_;
    std::condition_variable openCv_;
    bool openDone_ = false;
    std::wstring openError_;

    std::atomic<bool> failed_{ false };
    std::atomic<int64_t> writtenTicks_{ 0 };
    std::atomic<uint64_t> writtenBytes_{ 0 };
    Mp4WriteResult result_;
};

} // namespace clipback
