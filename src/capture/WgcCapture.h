// Windows Graphics Capture (Windows.Graphics.Capture) monitor capture.
// Primary capture path: works for windowed, borderless and (on Windows 10 2004+) exclusive fullscreen
// DX9/11/12/Vulkan/OpenGL games without any injection, and is anti-cheat safe (official OS API).
#pragma once
#include "capture/ICaptureSource.h"
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <mutex>
#include <atomic>

namespace clipback {

class WgcCapture final : public ICaptureSource {
public:
    WgcCapture() = default;
    ~WgcCapture() override;

    static bool IsSupported();

    bool Start(D3DDevice& device, HMONITOR monitor, bool captureCursor, FrameCallback callback, std::wstring& error) override;
    void Stop() override;
    const wchar_t* Name() const override { return L"Windows Graphics Capture"; }
    bool HasFailed(std::wstring& error) const override;

private:
    void OnFrameArrived(const winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool& sender,
                        const winrt::Windows::Foundation::IInspectable& args);
    void OnItemClosed(const winrt::Windows::Graphics::Capture::GraphicsCaptureItem&, const winrt::Windows::Foundation::IInspectable&);

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice_{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool_{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{ nullptr };
    winrt::event_token frameArrivedToken_{};
    winrt::event_token closedToken_{};
    winrt::Windows::Graphics::SizeInt32 lastSize_{};

    FrameCallback callback_;
    std::mutex callbackMutex_;
    std::atomic<bool> running_{ false };
    mutable std::mutex errorMutex_;
    std::wstring failure_;
};

} // namespace clipback
