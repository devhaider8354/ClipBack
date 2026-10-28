// DXGI Desktop Duplication fallback (Windows 8+). Used when Windows Graphics Capture is not available
// (e.g. Windows 10 builds before 1903) or fails to start.
#pragma once
#include "capture/ICaptureSource.h"
#include <thread>
#include <atomic>
#include <mutex>

namespace clipback {

class DuplicationCapture final : public ICaptureSource {
public:
    DuplicationCapture() = default;
    ~DuplicationCapture() override;

    bool Start(D3DDevice& device, HMONITOR monitor, bool captureCursor, FrameCallback callback, std::wstring& error) override;
    void Stop() override;
    const wchar_t* Name() const override { return L"DXGI Desktop Duplication"; }
    bool HasFailed(std::wstring& error) const override;

private:
    bool CreateDuplication(std::wstring& error);
    void ThreadMain();

    D3DDevice* device_ = nullptr;
    HMONITOR monitor_ = nullptr;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> copy_;          // private copy so DXGI can recycle its frame immediately
    UINT width_ = 0, height_ = 0;
    FrameCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{ false };
    mutable std::mutex errorMutex_;
    std::wstring failure_;
};

} // namespace clipback
