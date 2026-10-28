// H.264 encoder built on Media Foundation transforms.
// Picks NVENC / AMF / Quick Sync hardware MFTs (D3D11-aware: the NV12 texture is handed to the
// GPU encoder directly) and falls back to the Microsoft software H.264 MFT with a GPU->CPU readback.
#pragma once
#include "common/Common.h"
#include "capture/D3DDevice.h"
#include "buffer/RingBuffer.h"
#include "settings/Settings.h"
#include "system/SystemInfo.h"

#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>

#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>

namespace clipback {

struct VideoEncoderConfig {
    UINT width = 1920;
    UINT height = 1080;
    UINT fps = 60;
    UINT bitrateKbps = 12000;
    EncoderPreference preference = EncoderPreference::Auto;
    GpuVendor preferredVendor = GpuVendor::Unknown;   // vendor of the capture adapter (for Auto)
};

struct VideoEncoderStats {
    uint64_t framesSubmitted = 0;
    uint64_t framesEncoded = 0;
    uint64_t framesDropped = 0;
};

using PacketCallback = std::function<void(EncodedPacket&&)>;

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool Initialize(D3DDevice& device, const VideoEncoderConfig& config, PacketCallback onPacket, std::wstring& error);
    void Shutdown();

    // Non-blocking. The texture must stay valid until the encoder consumed it (pool rotation guarantees this).
    void Submit(ID3D11Texture2D* nv12, int64_t pts, int64_t duration);

    // Media type describing the produced stream (includes SPS/PPS). Null until the first packet was produced.
    ComPtr<IMFMediaType> OutputType() const;
    const EncoderInfo& SelectedEncoder() const { return selected_; }
    bool IsHardware() const { return selected_.hardware; }
    VideoEncoderStats Stats() const;
    bool HasFailed(std::wstring& error) const;

private:
    struct Candidate {
        ComPtr<IMFActivate> activate;
        EncoderInfo info;
    };
    struct PendingFrame {
        ComPtr<ID3D11Texture2D> texture;
        int64_t pts;
        int64_t duration;
    };

    class EventCallback;   // IMFAsyncCallback for async MFT events

    std::vector<Candidate> EnumerateCandidates();
    std::vector<Candidate> OrderCandidates(std::vector<Candidate> all);
    bool TryConfigure(Candidate& c, std::wstring& error);
    bool ConfigureTypes(std::wstring& error);
    void ApplyCodecSettings();
    void ReleaseTransform();

    void ThreadMain();
    HRESULT FeedInput(const PendingFrame& frame);
    HRESULT ProcessOutputOnce(bool& produced);
    void HandleOutputSample(IMFSample* sample);
    ComPtr<IMFSample> MakeSampleFromTexture(const PendingFrame& frame);
    ComPtr<IMFSample> MakeSampleByReadback(const PendingFrame& frame);
    void OnEvent(MediaEventType type, HRESULT status);
    void SetFailure(const std::wstring& msg);

    D3DDevice* device_ = nullptr;
    VideoEncoderConfig config_{};
    PacketCallback onPacket_;
    EncoderInfo selected_;

    ComPtr<IMFTransform> mft_;
    ComPtr<IMFMediaEventGenerator> eventGen_;
    ComPtr<EventCallback> eventCallback_;
    DWORD inputId_ = 0;
    DWORD outputId_ = 0;
    bool async_ = false;
    bool d3dAware_ = false;
    bool providesSamples_ = false;
    DWORD outputBufferSize_ = 0;
    ComPtr<ID3D11Texture2D> staging_;   // software path only

    // Thread coordination
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<PendingFrame> queue_;
    size_t maxQueue_ = 2;
    int needInput_ = 0;
    int haveOutput_ = 0;
    bool stop_ = false;

    mutable std::mutex typeMutex_;
    ComPtr<IMFMediaType> outputType_;
    bool firstOutput_ = true;

    mutable std::mutex statsMutex_;
    VideoEncoderStats stats_;
    std::wstring failure_;
};

} // namespace clipback
