#include "buffer/RingBuffer.h"

namespace clipback {

RingBuffer::RingBuffer(int64_t retainTicks) : retainTicks_(retainTicks) {}

void RingBuffer::SetRetention(int64_t retainTicks)
{
    std::lock_guard<std::mutex> lock(mutex_);
    retainTicks_ = retainTicks;
    TrimLocked();
}

void RingBuffer::PushVideo(EncodedPacket packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // The buffer must always begin with a keyframe so any snapshot is decodable.
    if (video_.empty() && !packet.keyframe) return;
    bytes_ += packet.size();
    video_.push_back(std::move(packet));
    TrimLocked();
}

void RingBuffer::PushAudio(EncodedPacket packet)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bytes_ += packet.size();
    audio_.push_back(std::move(packet));
    // Audio is trimmed against the video window in TrimLocked; if video has not started yet keep
    // at most the retention window of audio so the buffer cannot grow unbounded.
    if (video_.empty()) {
        const int64_t newest = audio_.back().pts;
        while (!audio_.empty() && newest - audio_.front().pts > retainTicks_ + kTicksPerSecond) {
            bytes_ -= audio_.front().size();
            audio_.pop_front();
        }
    }
}

void RingBuffer::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    video_.clear();
    audio_.clear();
    bytes_ = 0;
}

// Drops whole GOPs from the front while the buffer still holds >= retainTicks after the drop.
void RingBuffer::TrimLocked()
{
    if (video_.empty()) return;
    const int64_t newest = video_.back().pts + video_.back().duration;

    for (;;) {
        // Find the next keyframe after the first packet.
        size_t nextKey = 0;
        for (size_t i = 1; i < video_.size(); ++i) {
            if (video_[i].keyframe) { nextKey = i; break; }
        }
        if (nextKey == 0) break;  // only one GOP left
        if (newest - video_[nextKey].pts < retainTicks_) break;  // dropping would leave less than the window
        for (size_t i = 0; i < nextKey; ++i) {
            bytes_ -= video_.front().size();
            video_.pop_front();
        }
    }

    const int64_t start = video_.front().pts;
    while (!audio_.empty() && audio_.front().pts + audio_.front().duration < start) {
        bytes_ -= audio_.front().size();
        audio_.pop_front();
    }
}

bool RingBuffer::Snapshot(ReplaySnapshot& out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    out = ReplaySnapshot{};
    if (video_.empty() || !video_.front().keyframe) return false;

    const int64_t newest = video_.back().pts + video_.back().duration;
    // Choose the first keyframe such that the window is <= retention (+ one GOP tolerance handled by trim).
    size_t startIndex = 0;
    for (size_t i = 0; i < video_.size(); ++i) {
        if (video_[i].keyframe && newest - video_[i].pts <= retainTicks_) { startIndex = i; break; }
        if (video_[i].keyframe) startIndex = i;  // remember the last keyframe that is still too old
    }
    // If every keyframe is older than the window (single long GOP), start from the oldest keyframe.
    if (newest - video_[startIndex].pts > retainTicks_) {
        for (size_t i = 0; i < video_.size(); ++i) { if (video_[i].keyframe) { startIndex = i; break; } }
    }

    out.video.assign(video_.begin() + static_cast<std::ptrdiff_t>(startIndex), video_.end());
    out.startPts = out.video.front().pts;
    out.endPts = newest;
    for (const auto& a : audio_) {
        if (a.pts + a.duration <= out.startPts) continue;
        out.audio.push_back(a);
    }
    return true;
}

RingBufferStats RingBuffer::Stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    RingBufferStats s;
    s.bytes = bytes_;
    s.videoPackets = video_.size();
    s.audioPackets = audio_.size();
    if (!video_.empty()) {
        s.bufferedSeconds = static_cast<double>(video_.back().pts + video_.back().duration - video_.front().pts) / static_cast<double>(kTicksPerSecond);
    }
    return s;
}

} // namespace clipback
