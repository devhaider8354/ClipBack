#include "capture/WgcCapture.h"
#include "logging/Logger.h"

#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <inspectable.h>

#pragma comment(lib, "windowsapp.lib")

namespace clipback {

namespace WGC = winrt::Windows::Graphics::Capture;
namespace WGD = winrt::Windows::Graphics::DirectX;
namespace WGD3D = winrt::Windows::Graphics::DirectX::Direct3D11;

WgcCapture::~WgcCapture() { Stop(); }

bool WgcCapture::IsSupported()
{
    try {
        return WGC::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool WgcCapture::Start(D3DDevice& device, HMONITOR monitor, bool captureCursor, FrameCallback callback, std::wstring& error)
{
    Stop();
    {
        std::lock_guard<std::mutex> lock(callbackMutex_);
        callback_ = std::move(callback);
    }
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        failure_.clear();
    }

    try {
        // Wrap the D3D11 device for WinRT.
        winrt::com_ptr<::IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(device.DxgiDevice(), inspectable.put()));
        winrtDevice_ = inspectable.as<WGD3D::IDirect3DDevice>();

        // Capture item for the whole monitor (games in every window mode end up in the monitor image).
        auto interop = winrt::get_activation_factory<WGC::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        winrt::check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<WGC::GraphicsCaptureItem>(), winrt::put_abi(item_)));

        lastSize_ = item_.Size();
        if (lastSize_.Width <= 0 || lastSize_.Height <= 0) {
            error = L"Capture item reported an empty size";
            return false;
        }

        // Free-threaded pool: FrameArrived fires on a thread-pool thread, never on the UI thread.
        framePool_ = WGC::Direct3D11CaptureFramePool::CreateFreeThreaded(winrtDevice_, WGD::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, lastSize_);
        session_ = framePool_.CreateCaptureSession(item_);

        using winrt::Windows::Foundation::Metadata::ApiInformation;
        if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled")) {
            try { session_.IsCursorCaptureEnabled(captureCursor); } catch (const winrt::hresult_error& e) { LOG_WARN(L"IsCursorCaptureEnabled not applied: %s", e.message().c_str()); }
        }
        if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
            // Removes the yellow capture border on Windows 11. May require consent on some builds - ignore failures.
            try { session_.IsBorderRequired(false); } catch (const winrt::hresult_error& e) { LOG_DEBUG(L"IsBorderRequired(false) not applied: %s", e.message().c_str()); }
        }

        frameArrivedToken_ = framePool_.FrameArrived({ this, &WgcCapture::OnFrameArrived });
        closedToken_ = item_.Closed({ this, &WgcCapture::OnItemClosed });

        running_ = true;
        session_.StartCapture();
        LOG_INFO(L"WGC capture started (%dx%d, cursor=%d)", lastSize_.Width, lastSize_.Height, captureCursor ? 1 : 0);
        return true;
    } catch (const winrt::hresult_error& e) {
        error = L"Windows Graphics Capture failed: " + std::wstring(e.message().c_str()) + L" (" + logging::HResultToString(e.code()) + L")";
        Stop();
        return false;
    } catch (...) {
        error = L"Windows Graphics Capture failed with an unknown exception";
        Stop();
        return false;
    }
}

void WgcCapture::Stop()
{
    running_ = false;
    try {
        if (framePool_ && frameArrivedToken_.value) { framePool_.FrameArrived(frameArrivedToken_); frameArrivedToken_ = {}; }
        if (item_ && closedToken_.value) { item_.Closed(closedToken_); closedToken_ = {}; }
        if (session_) { session_.Close(); session_ = nullptr; }
        if (framePool_) { framePool_.Close(); framePool_ = nullptr; }
    } catch (...) {
        // Closing during device loss can throw; nothing else to do.
    }
    item_ = nullptr;
    winrtDevice_ = nullptr;
    // Make sure no callback is still executing before the owner tears down the pipeline.
    std::lock_guard<std::mutex> lock(callbackMutex_);
    callback_ = nullptr;
}

bool WgcCapture::HasFailed(std::wstring& error) const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    error = failure_;
    return !failure_.empty();
}

void WgcCapture::OnItemClosed(const WGC::GraphicsCaptureItem&, const winrt::Windows::Foundation::IInspectable&)
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    failure_ = L"The captured display was disconnected";
    LOG_WARN(L"WGC capture item closed");
}

void WgcCapture::OnFrameArrived(const WGC::Direct3D11CaptureFramePool& sender, const winrt::Windows::Foundation::IInspectable&)
{
    if (!running_) return;
    try {
        WGC::Direct3D11CaptureFrame frame = sender.TryGetNextFrame();
        if (!frame) return;

        const auto size = frame.ContentSize();
        if (size.Width != lastSize_.Width || size.Height != lastSize_.Height) {
            // Resolution changed (game switched display mode). Recreate the pool and drop this frame.
            lastSize_ = size;
            if (size.Width > 0 && size.Height > 0) {
                sender.Recreate(winrtDevice_, WGD::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
                LOG_INFO(L"WGC content size changed to %dx%d", size.Width, size.Height);
            }
            return;
        }

        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        ComPtr<ID3D11Texture2D> texture;
        winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&texture)));

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        CapturedFrame cf;
        cf.texture = texture.Get();
        cf.width = static_cast<UINT>(size.Width);
        cf.height = static_cast<UINT>(size.Height);
        cf.timestamp100ns = frame.SystemRelativeTime().count();
        if (cf.width > desc.Width) cf.width = desc.Width;
        if (cf.height > desc.Height) cf.height = desc.Height;

        std::lock_guard<std::mutex> lock(callbackMutex_);
        if (callback_ && running_) callback_(cf);
        // `frame` is closed by its destructor here, which releases the pool buffer.
    } catch (const winrt::hresult_error& e) {
        LOG_WARN(L"WGC FrameArrived error: %s", e.message().c_str());
        if (e.code() == DXGI_ERROR_DEVICE_REMOVED || e.code() == DXGI_ERROR_DEVICE_RESET) {
            std::lock_guard<std::mutex> lock(errorMutex_);
            failure_ = L"Graphics device was removed or reset";
        }
    } catch (...) {
        LOG_WARN(L"WGC FrameArrived unknown exception");
    }
}

} // namespace clipback
