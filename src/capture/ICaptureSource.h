// Abstract screen capture source. Frames are delivered as GPU textures (BGRA) on a worker thread.
#pragma once
#include "common/Common.h"
#include "capture/D3DDevice.h"
#include <functional>
#include <string>

namespace clipback {

struct CapturedFrame {
    ID3D11Texture2D* texture = nullptr;   // valid only during the callback
    UINT width = 0;
    UINT height = 0;
    int64_t timestamp100ns = 0;           // QPC-based, same clock as Now100ns()
};

using FrameCallback = std::function<void(const CapturedFrame&)>;

class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;
    virtual bool Start(D3DDevice& device, HMONITOR monitor, bool captureCursor, FrameCallback callback, std::wstring& error) = 0;
    virtual void Stop() = 0;
    virtual const wchar_t* Name() const = 0;
    // Set when the source hit an unrecoverable error after Start (e.g. display mode change with no recovery).
    virtual bool HasFailed(std::wstring& error) const = 0;
};

} // namespace clipback
