#include "capture/DuplicationCapture.h"
#include "logging/Logger.h"
#include <avrt.h>

#pragma comment(lib, "avrt.lib")

namespace clipback {

DuplicationCapture::~DuplicationCapture() { Stop(); }

bool DuplicationCapture::CreateDuplication(std::wstring& error)
{
    duplication_.Reset();
    copy_.Reset();

    IDXGIAdapter1* adapter = device_->Adapter();
    if (!adapter) { error = L"No DXGI adapter"; return false; }

    ComPtr<IDXGIOutput> output;
    for (UINT o = 0;; ++o) {
        ComPtr<IDXGIOutput> candidate;
        if (adapter->EnumOutputs(o, &candidate) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_OUTPUT_DESC od{};
        if (SUCCEEDED(candidate->GetDesc(&od)) && (od.Monitor == monitor_ || !monitor_)) { output = candidate; break; }
    }
    if (!output) { error = L"Monitor is not attached to the capture adapter"; return false; }

    ComPtr<IDXGIOutput1> output1;
    HRESULT hr = output.As(&output1);
    if (FAILED(hr)) { error = L"IDXGIOutput1 unavailable: " + logging::HResultToString(hr); return false; }

    hr = output1->DuplicateOutput(device_->Device(), &duplication_);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) error = L"Desktop Duplication limit reached (too many capture apps running)";
        else if (hr == E_ACCESSDENIED) error = L"Desktop Duplication access denied (secure desktop / UAC prompt active)";
        else error = L"DuplicateOutput failed: " + logging::HResultToString(hr);
        return false;
    }

    DXGI_OUTDUPL_DESC dd{};
    duplication_->GetDesc(&dd);
    width_ = dd.ModeDesc.Width;
    height_ = dd.ModeDesc.Height;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = width_;
    td.Height = height_;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    hr = device_->Device()->CreateTexture2D(&td, nullptr, &copy_);
    if (FAILED(hr)) { error = L"CreateTexture2D (duplication copy) failed: " + logging::HResultToString(hr); return false; }
    return true;
}

bool DuplicationCapture::Start(D3DDevice& device, HMONITOR monitor, bool /*captureCursor*/, FrameCallback callback, std::wstring& error)
{
    Stop();
    device_ = &device;
    monitor_ = monitor;
    callback_ = std::move(callback);
    {
        std::lock_guard<std::mutex> lock(errorMutex_);
        failure_.clear();
    }
    if (!CreateDuplication(error)) return false;
    running_ = true;
    thread_ = std::thread(&DuplicationCapture::ThreadMain, this);
    LOG_INFO(L"Desktop Duplication capture started (%ux%u)", width_, height_);
    return true;
}

void DuplicationCapture::Stop()
{
    running_ = false;
    if (thread_.joinable()) thread_.join();
    duplication_.Reset();
    copy_.Reset();
    callback_ = nullptr;
    device_ = nullptr;
}

bool DuplicationCapture::HasFailed(std::wstring& error) const
{
    std::lock_guard<std::mutex> lock(errorMutex_);
    error = failure_;
    return !failure_.empty();
}

void DuplicationCapture::ThreadMain()
{
    ComInitializer com;
    DWORD taskIndex = 0;
    HANDLE avrt = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);

    int consecutiveFailures = 0;
    while (running_) {
        if (!duplication_) {
            // Recover after access lost (mode change, fullscreen switch, UAC).
            std::wstring err;
            if (!CreateDuplication(err)) {
                if (++consecutiveFailures > 50) {   // ~10 s of retries
                    std::lock_guard<std::mutex> lock(errorMutex_);
                    failure_ = err;
                    break;
                }
                Sleep(200);
                continue;
            }
            consecutiveFailures = 0;
        }

        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        // AcquireNextFrame blocks (kernel wait, no CPU spin) until the desktop changes or the timeout elapses.
        HRESULT hr = duplication_->AcquireNextFrame(100, &info, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
            LOG_WARN(L"Desktop Duplication access lost - reinitialising");
            duplication_.Reset();
            continue;
        }
        if (FAILED(hr)) {
            LOG_HR(L"AcquireNextFrame failed", hr);
            duplication_.Reset();
            Sleep(100);
            continue;
        }

        if (info.LastPresentTime.QuadPart != 0 && resource) {
            ComPtr<ID3D11Texture2D> tex;
            if (SUCCEEDED(resource.As(&tex))) {
                device_->Context()->CopyResource(copy_.Get(), tex.Get());
                CapturedFrame cf;
                cf.texture = copy_.Get();
                cf.width = width_;
                cf.height = height_;
                cf.timestamp100ns = QpcTo100ns(info.LastPresentTime.QuadPart);
                duplication_->ReleaseFrame();
                if (callback_) callback_(cf);
                continue;
            }
        }
        duplication_->ReleaseFrame();
    }

    if (avrt) AvRevertMmThreadCharacteristics(avrt);
}

} // namespace clipback
