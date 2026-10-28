// Shared Direct3D 11 device used by capture, the GPU colour converter and the hardware encoder.
// One device for the whole pipeline means frames never leave VRAM.
#pragma once
#include "common/Common.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfobjects.h>
#include <mfapi.h>

namespace clipback {

class D3DDevice {
public:
    D3DDevice() = default;
    ~D3DDevice();
    D3DDevice(const D3DDevice&) = delete;
    D3DDevice& operator=(const D3DDevice&) = delete;

    // Creates a hardware device on the adapter that owns `monitor` (nullptr = default adapter).
    bool Create(HMONITOR monitor, std::wstring& error);
    void Destroy();

    ID3D11Device* Device() const { return device_.Get(); }
    ID3D11DeviceContext* Context() const { return context_.Get(); }
    IDXGIDevice* DxgiDevice() const { return dxgiDevice_.Get(); }
    IDXGIAdapter1* Adapter() const { return adapter_.Get(); }
    IMFDXGIDeviceManager* DeviceManager() const { return deviceManager_.Get(); }
    UINT DeviceManagerToken() const { return resetToken_; }
    bool VideoApiAvailable() const { return videoDevice_ != nullptr; }
    ID3D11VideoDevice* VideoDevice() const { return videoDevice_.Get(); }
    ID3D11VideoContext* VideoContext() const { return videoContext_.Get(); }
    D3D_FEATURE_LEVEL FeatureLevel() const { return featureLevel_; }

private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIDevice> dxgiDevice_;
    ComPtr<IDXGIAdapter1> adapter_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<IMFDXGIDeviceManager> deviceManager_;
    UINT resetToken_ = 0;
    D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
};

} // namespace clipback
