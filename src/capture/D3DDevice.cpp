#include "capture/D3DDevice.h"
#include "logging/Logger.h"
#include <dxgi1_6.h>
#include <d3d10_1.h>   // ID3D10Multithread

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "mfplat.lib")
#include <iterator>

namespace clipback {

D3DDevice::~D3DDevice() { Destroy(); }

static ComPtr<IDXGIAdapter1> FindAdapterForMonitor(HMONITOR monitor)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return nullptr;
    ComPtr<IDXGIAdapter1> firstHardware;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (!firstHardware) firstHardware = adapter;
        if (!monitor) continue;
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, &output) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od{};
            if (SUCCEEDED(output->GetDesc(&od)) && od.Monitor == monitor) return adapter;
        }
    }
    return firstHardware;
}

bool D3DDevice::Create(HMONITOR monitor, std::wstring& error)
{
    Destroy();

    adapter_ = FindAdapterForMonitor(monitor);

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDevice(adapter_.Get(), adapter_ ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
    if (hr == E_INVALIDARG) {
        // Older runtime that does not know 11_1
        hr = D3D11CreateDevice(adapter_.Get(), adapter_ ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels + 1, static_cast<UINT>(std::size(levels)) - 1, D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
    }
    if (FAILED(hr)) {
        // Video support flag can fail on exotic drivers - retry without it (software encoder path still works).
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(adapter_.Get(), adapter_ ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels + 1, static_cast<UINT>(std::size(levels)) - 1, D3D11_SDK_VERSION, &device_, &featureLevel_, &context_);
    }
    if (FAILED(hr)) {
        error = L"D3D11CreateDevice failed: " + logging::HResultToString(hr);
        return false;
    }

    // The device is touched from the capture thread, the encoder thread and MF worker threads.
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(device_.As(&mt)) && mt) mt->SetMultithreadProtected(TRUE);

    hr = device_.As(&dxgiDevice_);
    if (FAILED(hr)) { error = L"IDXGIDevice query failed: " + logging::HResultToString(hr); return false; }

    if (!adapter_) {
        ComPtr<IDXGIAdapter> a;
        if (SUCCEEDED(dxgiDevice_->GetAdapter(&a))) a.As(&adapter_);
    }

    device_.As(&videoDevice_);
    context_.As(&videoContext_);

    hr = MFCreateDXGIDeviceManager(&resetToken_, &deviceManager_);
    if (FAILED(hr)) { error = L"MFCreateDXGIDeviceManager failed: " + logging::HResultToString(hr); return false; }
    hr = deviceManager_->ResetDevice(device_.Get(), resetToken_);
    if (FAILED(hr)) { error = L"IMFDXGIDeviceManager::ResetDevice failed: " + logging::HResultToString(hr); return false; }

    if (adapter_) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter_->GetDesc1(&desc);
        LOG_INFO(L"D3D11 device created on '%s' (feature level 0x%X, video API %s)", desc.Description, featureLevel_, videoDevice_ ? L"yes" : L"no");
    }
    return true;
}

void D3DDevice::Destroy()
{
    deviceManager_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    if (context_) { context_->ClearState(); context_->Flush(); }
    context_.Reset();
    dxgiDevice_.Reset();
    adapter_.Reset();
    device_.Reset();
}

} // namespace clipback
