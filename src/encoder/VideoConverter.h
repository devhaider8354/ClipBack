// BGRA -> NV12 conversion + scaling on the GPU using the D3D11 Video Processor (fixed-function
// video engine on NVIDIA/AMD/Intel). No CPU readback, no shader, ~0 CPU cost per frame.
#pragma once
#include "common/Common.h"
#include "capture/D3DDevice.h"
#include <vector>
#include <mutex>

namespace clipback {

class VideoConverter {
public:
    VideoConverter() = default;
    ~VideoConverter();

    // outputWidth/outputHeight: encoder resolution (even numbers). poolSize: number of NV12 targets to rotate.
    bool Initialize(D3DDevice& device, UINT outputWidth, UINT outputHeight, UINT fps, UINT poolSize, std::wstring& error);
    void Shutdown();

    // Converts `source` (any BGRA size) into the next free NV12 texture of the pool.
    // Returns the NV12 texture (owned by the pool, valid until it is reused poolSize frames later).
    ID3D11Texture2D* Convert(ID3D11Texture2D* source, UINT sourceWidth, UINT sourceHeight, std::wstring& error);

    UINT OutputWidth() const { return outW_; }
    UINT OutputHeight() const { return outH_; }

private:
    bool CreateProcessor(UINT inW, UINT inH, std::wstring& error);

    D3DDevice* device_ = nullptr;
    UINT outW_ = 0, outH_ = 0, fps_ = 60;
    UINT inW_ = 0, inH_ = 0;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    std::vector<ComPtr<ID3D11Texture2D>> pool_;
    ComPtr<ID3D11Texture2D> intermediate_;   // only used when the driver rejects the capture texture as VP input
    UINT intermediateW_ = 0, intermediateH_ = 0;
    std::vector<ComPtr<ID3D11VideoProcessorOutputView>> outputViews_;
    size_t next_ = 0;
    std::mutex mutex_;
};

} // namespace clipback
