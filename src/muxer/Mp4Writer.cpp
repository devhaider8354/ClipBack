#include "muxer/Mp4Writer.h"
#include "logging/Logger.h"
#include <mfapi.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")

namespace clipback {

namespace {

ComPtr<IMFSample> MakeSample(const EncodedPacket& pkt, int64_t rebasedPts)
{
    ComPtr<IMFMediaBuffer> buffer;
    const DWORD len = static_cast<DWORD>(pkt.size());
    if (FAILED(MFCreateMemoryBuffer(len, &buffer))) return nullptr;
    BYTE* dst = nullptr;
    if (FAILED(buffer->Lock(&dst, nullptr, nullptr))) return nullptr;
    memcpy(dst, pkt.data->data(), len);
    buffer->Unlock();
    buffer->SetCurrentLength(len);

    ComPtr<IMFSample> sample;
    if (FAILED(MFCreateSample(&sample))) return nullptr;
    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(rebasedPts);
    sample->SetSampleDuration(pkt.duration);
    if (pkt.keyframe) sample->SetUINT32(MFSampleExtension_CleanPoint, 1);
    return sample;
}

ComPtr<IMFMediaType> CloneType(IMFMediaType* src)
{
    ComPtr<IMFMediaType> t;
    if (FAILED(MFCreateMediaType(&t))) return nullptr;
    if (FAILED(src->CopyAllItems(t.Get()))) return nullptr;
    return t;
}

} // namespace

Mp4WriteResult Mp4Writer::Write(const std::wstring& path, const ReplaySnapshot& snapshot, IMFMediaType* videoType, IMFMediaType* audioType)
{
    Mp4WriteResult result;
    result.path = path;
    if (snapshot.video.empty() || !videoType) {
        result.error = L"Nothing to save yet (the buffer has no complete video segment)";
        return result;
    }

    ComPtr<IMFAttributes> attrs;
    MFCreateAttributes(&attrs, 4);
    attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
    attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    attrs->SetUINT32(MF_LOW_LATENCY, FALSE);

    ComPtr<IMFSinkWriter> writer;
    HRESULT hr = MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &writer);
    if (FAILED(hr)) {
        result.error = L"Cannot create MP4 file: " + logging::HResultToString(hr);
        return result;
    }

    // Video stream - identical input and output type => pass-through, no transcoding.
    ComPtr<IMFMediaType> vOut = CloneType(videoType);
    if (!vOut) { result.error = L"Cannot clone video media type"; return result; }
    DWORD videoIndex = 0;
    hr = writer->AddStream(vOut.Get(), &videoIndex);
    if (FAILED(hr)) { result.error = L"AddStream(video) failed: " + logging::HResultToString(hr); return result; }
    hr = writer->SetInputMediaType(videoIndex, vOut.Get(), nullptr);
    if (FAILED(hr)) { result.error = L"SetInputMediaType(video) failed: " + logging::HResultToString(hr); return result; }

    std::array<DWORD, kAudioTrackCount> audioIndex{};
    std::array<bool, kAudioTrackCount> hasAudio{};
    if (audioType) {
        for (size_t track = 0; track < kAudioTrackCount; ++track) {
            if (snapshot.audio[track].empty()) continue;
            ComPtr<IMFMediaType> aOut = CloneType(audioType);
            if (aOut && SUCCEEDED(writer->AddStream(aOut.Get(), &audioIndex[track])) && SUCCEEDED(writer->SetInputMediaType(audioIndex[track], aOut.Get(), nullptr))) hasAudio[track] = true;
            else LOG_WARN(L"Audio track %zu could not be added to the MP4", track);
        }
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) { result.error = L"BeginWriting failed: " + logging::HResultToString(hr); return result; }

    struct WriteItem { const EncodedPacket* packet; size_t track; };
    std::vector<WriteItem> items;
    items.reserve(snapshot.video.size() + snapshot.audio[0].size() + snapshot.audio[1].size() + snapshot.audio[2].size());
    for (const auto& p : snapshot.video) items.push_back({ &p, kAudioTrackCount });
    for (size_t track = 0; track < kAudioTrackCount; ++track) if (hasAudio[track]) for (const auto& p : snapshot.audio[track]) items.push_back({ &p, track });
    std::stable_sort(items.begin(), items.end(), [](const WriteItem& a, const WriteItem& b) { return a.packet->pts < b.packet->pts; });
    const int64_t t0 = snapshot.startPts;
    for (const WriteItem& item : items) {
        const bool takeAudio = item.track != kAudioTrackCount;
        const EncodedPacket& pkt = *item.packet;
        const DWORD index = takeAudio ? audioIndex[item.track] : videoIndex;

        int64_t pts = pkt.pts - t0;
        if (pts < 0) {
            if (takeAudio) continue;   // audio before the first keyframe
            pts = 0;
        }
        ComPtr<IMFSample> sample = MakeSample(pkt, pts);
        if (!sample) { result.error = L"Out of memory building sample"; writer->Finalize(); return result; }
        hr = writer->WriteSample(index, sample.Get());
        if (FAILED(hr)) {
            result.error = std::wstring(takeAudio ? L"WriteSample(audio) failed: " : L"WriteSample(video) failed: ") + logging::HResultToString(hr);
            writer->Finalize();
            return result;
        }
    }

    hr = writer->Finalize();
    if (FAILED(hr)) { result.error = L"Finalize failed: " + logging::HResultToString(hr); return result; }

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        result.bytes = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }
    result.durationSeconds = static_cast<double>(snapshot.DurationTicks()) / kTicksPerSecond;
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// Mp4StreamWriter - continuous (whole match) recording
// ---------------------------------------------------------------------------

Mp4StreamWriter::~Mp4StreamWriter() { Close(); }

bool Mp4StreamWriter::Open(const std::wstring& path, IMFMediaType* videoType, const AudioMediaTypes& audioTypes, std::wstring& error)
{
    if (thread_.joinable()) { error = L"Recording is already running"; return false; }
    if (!videoType) { error = L"Nothing recorded yet - wait a second and try again"; return false; }
    path_ = path;
    result_ = Mp4WriteResult{};
    result_.path = path;
    failed_ = false;
    writtenTicks_ = 0;
    writtenBytes_ = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        stop_ = false;
        accepting_ = true;
    }
    {
        std::lock_guard<std::mutex> lock(openMutex_);
        openDone_ = false;
        openError_.clear();
    }
    thread_ = std::thread(&Mp4StreamWriter::WorkerMain, this, ComPtr<IMFMediaType>(videoType), audioTypes);

    std::unique_lock<std::mutex> lock(openMutex_);
    openCv_.wait(lock, [&] { return openDone_; });
    if (!openError_.empty()) {
        error = openError_;
        lock.unlock();
        {
            std::lock_guard<std::mutex> q(mutex_);
            accepting_ = false;
            queue_.clear();
        }
        thread_.join();
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

void Mp4StreamWriter::Push(const EncodedPacket& pkt, bool audio, AudioTrack track)
{
    if (!pkt.data || pkt.data->empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_) return;
        queue_.push_back({ pkt, audio, track });   // shares the packet bytes, no copy
    }
    cv_.notify_one();
}

void Mp4StreamWriter::PushVideo(const EncodedPacket& pkt) { Push(pkt, false); }
void Mp4StreamWriter::PushAudio(AudioTrack track, const EncodedPacket& pkt) { Push(pkt, true, track); }

Mp4WriteResult Mp4StreamWriter::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    return result_;
}

void Mp4StreamWriter::WorkerMain(ComPtr<IMFMediaType> videoType, AudioMediaTypes audioTypes)
{
    ComInitializer com;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    auto signalOpen = [&](const std::wstring& err) {
        std::lock_guard<std::mutex> lock(openMutex_);
        openError_ = err.empty() ? std::wstring() : err;
        openDone_ = true;
        openCv_.notify_all();
    };

    ComPtr<IMFSinkWriter> writer;
    DWORD videoIndex = 0;
    std::array<DWORD, kAudioTrackCount> audioIndex{};
    std::array<bool, kAudioTrackCount> hasAudio{};
    {
        ComPtr<IMFAttributes> attrs;
        MFCreateAttributes(&attrs, 4);
        attrs->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
        attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
        attrs->SetUINT32(MF_LOW_LATENCY, FALSE);

        HRESULT hr = MFCreateSinkWriterFromURL(path_.c_str(), nullptr, attrs.Get(), &writer);
        if (FAILED(hr)) { signalOpen(L"Cannot create MP4 file: " + logging::HResultToString(hr)); return; }
        ComPtr<IMFMediaType> vOut = CloneType(videoType.Get());
        if (!vOut) { signalOpen(L"Cannot clone video media type"); return; }
        hr = writer->AddStream(vOut.Get(), &videoIndex);
        if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(videoIndex, vOut.Get(), nullptr);
        if (FAILED(hr)) { signalOpen(L"Video stream setup failed: " + logging::HResultToString(hr)); return; }
        for (size_t track = 0; track < kAudioTrackCount; ++track) if (audioTypes.tracks[track]) {
            ComPtr<IMFMediaType> aOut = CloneType(audioTypes.tracks[track].Get());
            if (aOut && SUCCEEDED(writer->AddStream(aOut.Get(), &audioIndex[track])) && SUCCEEDED(writer->SetInputMediaType(audioIndex[track], aOut.Get(), nullptr))) hasAudio[track] = true;
            else LOG_WARN(L"Match recording: audio track %zu could not be added", track);
        }
        hr = writer->BeginWriting();
        if (FAILED(hr)) { signalOpen(L"BeginWriting failed: " + logging::HResultToString(hr)); return; }
    }
    signalOpen(L"");
    LOG_INFO(L"Match recording started: %s", path_.c_str());

    int64_t base = -1, lastVideoPts = -1, lastTick = 0;
    std::array<int64_t, kAudioTrackCount> lastAudioPts{};
    std::array<int64_t, kAudioTrackCount> lastAudioWall{};
    std::array<std::vector<EncodedPacket>, kAudioTrackCount> heldAudio;
    lastAudioPts.fill(-1);
    lastAudioWall.fill(Now100ns());
    std::wstring error;

    auto write = [&](const EncodedPacket& pkt, bool audio, AudioTrack track = AudioTrack::Mix) {
        if (failed_) return;
        const int64_t pts = pkt.pts - base;
        const size_t trackIndex = static_cast<size_t>(track);
        if (audio) { if (pts < 0 || pts <= lastAudioPts[trackIndex]) return; }
        else if (pts <= lastVideoPts) return;
        ComPtr<IMFSample> sample = MakeSample(pkt, pts);
        HRESULT hr = sample ? writer->WriteSample(audio ? audioIndex[trackIndex] : videoIndex, sample.Get()) : E_OUTOFMEMORY;
        if (FAILED(hr)) {
            error = std::wstring(audio ? L"Writing audio failed: " : L"Writing video failed: ") + logging::HResultToString(hr);
            LOG_ERROR(L"Match recording: %s", error.c_str());
            failed_ = true;
            return;
        }
        writtenBytes_ += pkt.size();
        if (audio) { lastAudioPts[trackIndex] = pts; lastAudioWall[trackIndex] = Now100ns(); }
        else { lastVideoPts = pts; writtenTicks_ = pts + pkt.duration; }
    };

    for (;;) {
        std::deque<Item> batch;
        bool stopping;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(500), [&] { return stop_ || !queue_.empty(); });
            batch.swap(queue_);
            stopping = stop_;
        }
        for (const Item& it : batch) {
            if (it.audio) {
                const size_t track = static_cast<size_t>(it.track);
                if (!hasAudio[track]) continue;
                if (base < 0) { if (heldAudio[track].size() < 4000) heldAudio[track].push_back(it.pkt); continue; }
                write(it.pkt, true, it.track);
            } else {
                if (base < 0) {
                    if (!it.pkt.keyframe) continue;   // the file must start on a keyframe
                    base = it.pkt.pts;
                    for (size_t track = 0; track < kAudioTrackCount; ++track) {
                        for (const EncodedPacket& a : heldAudio[track]) write(a, true, static_cast<AudioTrack>(track));
                        heldAudio[track].clear();
                    }
                }
                write(it.pkt, false);
            }
        }
        // Audio stopped (device lost): tell the sink not to wait for it, so video keeps flowing to disk.
        const int64_t now = Now100ns();
        if (base >= 0 && !failed_ && now - lastTick > kTicksPerSecond && lastVideoPts >= 0) {
            lastTick = now;
            for (size_t track = 0; track < kAudioTrackCount; ++track) if (hasAudio[track] && now - lastAudioWall[track] > 2 * kTicksPerSecond) writer->SendStreamTick(audioIndex[track], lastVideoPts);
        }
        if (stopping) break;
    }

    const HRESULT hr = writer->Finalize();
    writer.Reset();
    if (base < 0) {
        result_.error = L"No video was recorded";
        DeleteFileW(path_.c_str());
        LOG_WARN(L"Match recording: no video recorded - file removed");
        return;
    }
    if (FAILED(hr)) {
        result_.error = L"Finalize failed: " + logging::HResultToString(hr) + (error.empty() ? L"" : L" / " + error);
        LOG_ERROR(L"Match recording: %s", result_.error.c_str());
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path_.c_str(), GetFileExInfoStandard, &fad)) {
        result_.bytes = (static_cast<uint64_t>(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
    }
    result_.durationSeconds = static_cast<double>(writtenTicks_.load()) / kTicksPerSecond;
    result_.error = error;   // non-empty: the file is valid up to the write error
    result_.ok = true;
    LOG_INFO(L"Match recording saved: %s (%.0f s, %llu bytes)", path_.c_str(), result_.durationSeconds, static_cast<unsigned long long>(result_.bytes));
}

} // namespace clipback
