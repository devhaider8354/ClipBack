// In-memory ring buffer of *encoded* H.264 / AAC packets.
//
// Why RAM only: the encoder output is already compressed, so the buffer size is simply
// bitrate x duration:  12 Mbps x 60 s = 90 MB,  20 Mbps x 120 s = 300 MB (absolute worst case).
// That is far cheaper than any disk-backed scheme (which would add constant SSD writes - exactly what
// the user asked to avoid) and there is nothing to "hybridise": packets are already the smallest
// representation of the video. Packets are reference counted so a save snapshot never copies bytes.
#pragma once
#include "common/Common.h"
#include <deque>
#include <mutex>
#include <memory>
#include <vector>

namespace clipback {

struct EncodedPacket {
    int64_t pts = 0;          // 100ns, relative to session start
    int64_t duration = 0;     // 100ns
    bool keyframe = false;
    std::shared_ptr<const std::vector<uint8_t>> data;
    size_t size() const { return data ? data->size() : 0; }
};

struct ReplaySnapshot {
    std::vector<EncodedPacket> video;   // starts on a keyframe
    std::vector<EncodedPacket> audio;
    int64_t startPts = 0;               // pts of first video packet
    int64_t endPts = 0;                 // pts + duration of last video packet
    int64_t DurationTicks() const { return endPts - startPts; }
};

struct RingBufferStats {
    double bufferedSeconds = 0.0;
    size_t bytes = 0;
    size_t videoPackets = 0;
    size_t audioPackets = 0;
};

class RingBuffer {
public:
    explicit RingBuffer(int64_t retainTicks = 60 * kTicksPerSecond);

    void SetRetention(int64_t retainTicks);
    void PushVideo(EncodedPacket packet);
    void PushAudio(EncodedPacket packet);
    void Clear();

    // Copies packet handles (not bytes) for the last `retain` window. Returns false if there is no keyframe yet.
    bool Snapshot(ReplaySnapshot& out) const;
    RingBufferStats Stats() const;

private:
    void TrimLocked();

    mutable std::mutex mutex_;
    int64_t retainTicks_;
    std::deque<EncodedPacket> video_;
    std::deque<EncodedPacket> audio_;
    size_t bytes_ = 0;
};

} // namespace clipback
