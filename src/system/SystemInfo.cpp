#include "system/SystemInfo.h"
#include "common/StringUtils.h"
#include "logging/Logger.h"

#include <dxgi1_6.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <iterator>
#include <propvarutil.h>
#include <shellscalingapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shcore.lib")

namespace clipback {

// PKEY_Device_FriendlyName without relying on INITGUID being defined in this translation unit.
static const PROPERTYKEY kPKEY_Device_FriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

const wchar_t* VendorName(GpuVendor v)
{
    switch (v) {
    case GpuVendor::Nvidia: return L"NVIDIA";
    case GpuVendor::Amd: return L"AMD";
    case GpuVendor::Intel: return L"Intel";
    case GpuVendor::Microsoft: return L"Microsoft";
    default: return L"Unknown";
    }
}

GpuVendor VendorFromId(UINT vendorId)
{
    switch (vendorId) {
    case 0x10DE: return GpuVendor::Nvidia;
    case 0x1002: case 0x1022: return GpuVendor::Amd;
    case 0x8086: return GpuVendor::Intel;
    case 0x1414: return GpuVendor::Microsoft;
    default: return GpuVendor::Unknown;
    }
}

GpuVendor VendorFromName(const std::wstring& name)
{
    if (str::ContainsNoCase(name, L"nvidia") || str::ContainsNoCase(name, L"nvenc") || str::ContainsNoCase(name, L"geforce")) return GpuVendor::Nvidia;
    if (str::ContainsNoCase(name, L"amd") || str::ContainsNoCase(name, L"radeon") || str::ContainsNoCase(name, L"amf")) return GpuVendor::Amd;
    if (str::ContainsNoCase(name, L"intel") || str::ContainsNoCase(name, L"quick sync") || str::ContainsNoCase(name, L"qsv")) return GpuVendor::Intel;
    if (str::ContainsNoCase(name, L"microsoft")) return GpuVendor::Microsoft;
    return GpuVendor::Unknown;
}

// ---------------------------------------------------------------------------
// GPU enumeration via DXGI
// ---------------------------------------------------------------------------

static std::vector<GpuInfo> EnumerateGpus()
{
    std::vector<GpuInfo> result;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return result;

    ComPtr<IDXGIFactory6> factory6;
    factory.As(&factory6);

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr;
        if (factory6) {
            hr = factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        } else {
            hr = factory->EnumAdapters1(i, &adapter);
        }
        if (hr == DXGI_ERROR_NOT_FOUND || FAILED(hr)) break;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;

        GpuInfo g;
        g.name = desc.Description;
        g.vendorId = desc.VendorId;
        g.vendor = VendorFromId(desc.VendorId);
        if (g.vendor == GpuVendor::Unknown) g.vendor = VendorFromName(g.name);
        g.dedicatedVideoMemory = desc.DedicatedVideoMemory;
        g.sharedSystemMemory = desc.SharedSystemMemory;
        g.adapterLuid = desc.AdapterLuid;
        g.isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        result.push_back(g);
    }
    return result;
}

static std::wstring ProbeFeatureLevel()
{
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got = D3D_FEATURE_LEVEL_9_1;
    ComPtr<ID3D11Device> dev;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &dev, &got, nullptr);
    if (FAILED(hr)) return L"unavailable";
    switch (got) {
    case D3D_FEATURE_LEVEL_12_1: return L"12_1";
    case D3D_FEATURE_LEVEL_12_0: return L"12_0";
    case D3D_FEATURE_LEVEL_11_1: return L"11_1";
    case D3D_FEATURE_LEVEL_11_0: return L"11_0";
    case D3D_FEATURE_LEVEL_10_1: return L"10_1";
    case D3D_FEATURE_LEVEL_10_0: return L"10_0";
    default: return L"9_x";
    }
}

static bool ProbeD3D12()
{
    // Only check for runtime presence without linking d3d12.lib so the app still starts on old systems.
    HMODULE mod = LoadLibraryW(L"d3d12.dll");
    if (!mod) return false;
    typedef HRESULT(WINAPI* PFN_Create)(IUnknown*, int, REFIID, void**);
    auto create = reinterpret_cast<PFN_Create>(GetProcAddress(mod, "D3D12CreateDevice"));
    bool ok = false;
    if (create) {
        // IID_ID3D12Device = {189819f1-1db6-4b57-be54-1821339b85f7}
        static const GUID iidDevice = { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
        // D3D_FEATURE_LEVEL_11_0 = 0xb000; passing nullptr for the device only tests support.
        ok = SUCCEEDED(create(nullptr, 0xb000, iidDevice, nullptr));
    }
    FreeLibrary(mod);
    return ok;
}

static std::wstring ProbeWindowsVersion()
{
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionPtr>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            fn(&vi);
        }
    }
    const wchar_t* name = vi.dwBuildNumber >= 22000 ? L"Windows 11" : L"Windows 10";
    return str::Format(L"%s (%u.%u build %u)", name, vi.dwMajorVersion, vi.dwMinorVersion, vi.dwBuildNumber);
}

// ---------------------------------------------------------------------------
// Video encoder enumeration (Media Foundation transforms)
// ---------------------------------------------------------------------------

std::vector<EncoderInfo> EnumerateVideoEncoders()
{
    std::vector<EncoderInfo> result;

    MFT_REGISTER_TYPE_INFO inType{ MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO outType{ MFMediaType_Video, MFVideoFormat_H264 };

    const UINT32 flagSets[] = {
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER,
    };

    for (UINT32 flags : flagSets) {
        IMFActivate** activates = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER, flags, &inType, &outType, &activates, &count);
        if (FAILED(hr)) {
            LOG_WARN(L"MFTEnumEx(video encoders, flags=0x%X) failed: %s", flags, logging::HResultToString(hr).c_str());
            continue;
        }
        for (UINT32 i = 0; i < count; ++i) {
            EncoderInfo info;
            LPWSTR name = nullptr;
            UINT32 len = 0;
            if (SUCCEEDED(activates[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &name, &len)) && name) {
                info.friendlyName = name;
                CoTaskMemFree(name);
            }
            LPWSTR ven = nullptr;
            if (SUCCEEDED(activates[i]->GetAllocatedString(MFT_ENUM_HARDWARE_VENDOR_ID_Attribute, &ven, &len)) && ven) {
                info.vendorIdString = ven;
                CoTaskMemFree(ven);
            }
            UINT32 mftFlags = 0;
            activates[i]->GetUINT32(MF_TRANSFORM_FLAGS_Attribute, &mftFlags);
            info.hardware = (mftFlags & MFT_ENUM_FLAG_HARDWARE) != 0 || (flags & MFT_ENUM_FLAG_HARDWARE) != 0;
            info.async = (mftFlags & MFT_ENUM_FLAG_ASYNCMFT) != 0 || info.hardware;

            if (str::ContainsNoCase(info.vendorIdString, L"10DE")) info.vendor = GpuVendor::Nvidia;
            else if (str::ContainsNoCase(info.vendorIdString, L"1002")) info.vendor = GpuVendor::Amd;
            else if (str::ContainsNoCase(info.vendorIdString, L"8086")) info.vendor = GpuVendor::Intel;
            else info.vendor = VendorFromName(info.friendlyName);

            bool duplicate = false;
            for (const auto& e : result) if (e.friendlyName == info.friendlyName) duplicate = true;
            if (!duplicate) result.push_back(info);
            activates[i]->Release();
        }
        CoTaskMemFree(activates);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Audio devices (WASAPI)
// ---------------------------------------------------------------------------

std::vector<AudioDeviceInfo> EnumerateAudioDevices(bool capture)
{
    std::vector<AudioDeviceInfo> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return result;

    const EDataFlow flow = capture ? eCapture : eRender;
    std::wstring defaultId;
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &def)) && def) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id)) && id) { defaultId = id; CoTaskMemFree(id); }
    }

    ComPtr<IMMDeviceCollection> devices;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))) return result;
    UINT count = 0;
    devices->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(devices->Item(i, &device))) continue;
        AudioDeviceInfo info;
        LPWSTR id = nullptr;
        if (SUCCEEDED(device->GetId(&id)) && id) { info.id = id; CoTaskMemFree(id); }
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(props->GetValue(kPKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR) info.name = pv.pwszVal;
            PropVariantClear(&pv);
        }
        if (info.name.empty()) info.name = L"Audio device " + std::to_wstring(i + 1);
        info.isDefault = (info.id == defaultId);
        result.push_back(info);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Monitors
// ---------------------------------------------------------------------------

static BOOL CALLBACK MonitorEnumProc(HMONITOR mon, HDC, LPRECT, LPARAM param)
{
    auto* list = reinterpret_cast<std::vector<MonitorInfo>*>(param);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi)) return TRUE;
    MonitorInfo info;
    info.handle = mon;
    info.deviceName = mi.szDevice;
    info.width = mi.rcMonitor.right - mi.rcMonitor.left;
    info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        info.refreshHz = static_cast<int>(dm.dmDisplayFrequency);
        // Physical pixels (rcMonitor may be virtualised on high-DPI when the process is not DPI aware).
        if (dm.dmPelsWidth && dm.dmPelsHeight) { info.width = static_cast<int>(dm.dmPelsWidth); info.height = static_cast<int>(dm.dmPelsHeight); }
    }
    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) info.dpi = dpiX;
    list->push_back(info);
    return TRUE;
}

std::vector<MonitorInfo> EnumerateMonitors()
{
    std::vector<MonitorInfo> list;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&list));
    // Primary first.
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].primary && i != 0) { std::swap(list[0], list[i]); break; }
    }
    return list;
}

// ---------------------------------------------------------------------------
// Aggregate
// ---------------------------------------------------------------------------

SystemInfo CollectSystemInfo()
{
    SystemInfo info;
    info.gpus = EnumerateGpus();
    info.d3dFeatureLevel = ProbeFeatureLevel();
    info.d3d12Available = ProbeD3D12();
    info.windowsVersion = ProbeWindowsVersion();
    try {
        info.graphicsCaptureSupported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        info.graphicsCaptureSupported = false;
    }
    info.videoEncoders = EnumerateVideoEncoders();
    info.renderDevices = EnumerateAudioDevices(false);
    info.captureDevices = EnumerateAudioDevices(true);
    info.monitors = EnumerateMonitors();

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) info.totalPhysicalMemory = mem.ullTotalPhys;
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    info.logicalProcessors = static_cast<int>(si.dwNumberOfProcessors);
    return info;
}

std::wstring SystemInfo::ToReport() const
{
    std::wstring r;
    r += L"ClipBack Diagnostics\r\n";
    r += L"===========================\r\n\r\n";
    r += L"OS: " + windowsVersion + L"\r\n";
    r += str::Format(L"CPU threads: %d\r\n", logicalProcessors);
    r += L"RAM: " + str::BytesToHuman(totalPhysicalMemory) + L"\r\n";
    r += L"Direct3D 11 feature level: " + d3dFeatureLevel + L"\r\n";
    r += std::wstring(L"Direct3D 12: ") + (d3d12Available ? L"available" : L"not available") + L"\r\n";
    r += std::wstring(L"Windows Graphics Capture: ") + (graphicsCaptureSupported ? L"supported" : L"NOT supported (Desktop Duplication fallback will be used)") + L"\r\n";

    r += L"\r\nGPUs:\r\n";
    if (gpus.empty()) r += L"  (none detected)\r\n";
    for (const auto& g : gpus) {
        if (g.isSoftware) continue;
        r += str::Format(L"  %s  [%s]  VRAM: %s  Shared: %s\r\n", g.name.c_str(), VendorName(g.vendor),
            str::BytesToHuman(g.dedicatedVideoMemory).c_str(), str::BytesToHuman(g.sharedSystemMemory).c_str());
    }

    r += L"\r\nH.264 encoders:\r\n";
    if (videoEncoders.empty()) r += L"  (none found)\r\n";
    for (const auto& e : videoEncoders) {
        r += str::Format(L"  %s  [%s, %s]\r\n", e.friendlyName.c_str(), VendorName(e.vendor), e.hardware ? L"hardware" : L"software");
    }

    r += L"\r\nMonitors:\r\n";
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        r += str::Format(L"  #%u %s  %dx%d @ %d Hz  DPI %u%s\r\n", static_cast<unsigned>(i), m.deviceName.c_str(), m.width, m.height, m.refreshHz, m.dpi, m.primary ? L"  (primary)" : L"");
    }

    r += L"\r\nAudio output devices:\r\n";
    if (renderDevices.empty()) r += L"  (none)\r\n";
    for (const auto& d : renderDevices) r += L"  " + d.name + (d.isDefault ? L"  (default)" : L"") + L"\r\n";
    r += L"\r\nMicrophones:\r\n";
    if (captureDevices.empty()) r += L"  (none)\r\n";
    for (const auto& d : captureDevices) r += L"  " + d.name + (d.isDefault ? L"  (default)" : L"") + L"\r\n";
    return r;
}

} // namespace clipback
