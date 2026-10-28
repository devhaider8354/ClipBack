// AAC-LC encoder (Microsoft AAC MFT, synchronous). Input: 48 kHz stereo 16-bit PCM.
#pragma once
#include "common/Common.h"
#include "buffer/RingBuffer.h"
#include <mfidl.h>
#include <mftransform.h>
#include <functional>
#include <vector>

namespace clipback {

class AacEncoder {
public:
    AacEncoder() = default;
    ~AacEncoder();
    AacEncoder(const AacEncoder&) = delete;
    AacEncoder& operator=(const AacEncoder&) = delete;

    bool Initialize(UINT bitrateKbps, std::function<void(EncodedPacket&&)> onPacket, std::wstring& error);
    void Shutdown();

    // pcm: interleaved int16 stereo. frames: number of sample frames. pts: 100ns timestamp of the first frame.
    bool Encode(const int16_t* pcm, size_t frames, int64_t pts);

    ComPtr<IMFMediaType> OutputType() const { return outputType_; }

private:
    void DrainOutput();

    ComPtr<IMFTransform> mft_;
    ComPtr<IMFMediaType> outputType_;
    std::function<void(EncodedPacket&&)> onPacket_;
    DWORD outputBufferSize_ = 0;
    bool providesSamples_ = false;
    int64_t framesFed_ = 0;
    int64_t basePts_ = -1;
    int64_t outputFrames_ = 0;
};

} // namespace clipback
