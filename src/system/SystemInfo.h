// Hardware / OS detection used by the Diagnostics page and by encoder selection.
#pragma once
#include "common/Common.h"
#include <string>
#include <vector>

namespace clipback {

enum class GpuVendor { Unknown, Nvidia, Amd, Intel, Microsoft };

struct GpuInfo {
    std::wstring name;
    GpuVendor vendor = GpuVendor::Unknown;
    UINT vendorId = 0;
    uint64_t dedicatedVideoMemory = 0;
    uint64_t sharedSystemMemory = 0;
    LUID adapterLuid{};
    bool isSoftware = false;
};

struct EncoderInfo {
    std::wstring friendlyName;
    GpuVendor vendor = GpuVendor::Unknown;
    bool hardware = false;
    bool async = false;
    std::wstring vendorIdString;   // e.g. "VEN_10DE"
};

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    bool isDefault = false;
};

struct MonitorInfo {
    HMONITOR handle = nullptr;
    std::wstring deviceName;
    int width = 0;
    int height = 0;
    int refreshHz = 0;
    bool primary = false;
    UINT dpi = 96;
};

struct SystemInfo {
    std::vector<GpuInfo> gpus;
    std::wstring d3dFeatureLevel;      // e.g. "11_1"
    bool d3d12Available = false;
    std::wstring windowsVersion;       // "Windows 11 (build 22631)"
    bool graphicsCaptureSupported = false;
    std::vector<EncoderInfo> videoEncoders;
    std::vector<AudioDeviceInfo> renderDevices;
    std::vector<AudioDeviceInfo> captureDevices;
    std::vector<MonitorInfo> monitors;
    uint64_t totalPhysicalMemory = 0;
    int logicalProcessors = 0;

    std::wstring ToReport() const;
};

const wchar_t* VendorName(GpuVendor v);
GpuVendor VendorFromId(UINT vendorId);
GpuVendor VendorFromName(const std::wstring& name);

SystemInfo CollectSystemInfo();
std::vector<EncoderInfo> EnumerateVideoEncoders();
std::vector<AudioDeviceInfo> EnumerateAudioDevices(bool capture);
std::vector<MonitorInfo> EnumerateMonitors();

} // namespace clipback
