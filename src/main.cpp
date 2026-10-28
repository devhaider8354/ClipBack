// ClipBack - entry point.
#include "common/Common.h"
#include "logging/Logger.h"
#include "settings/Settings.h"
#include "system/Paths.h"
#include "system/SystemInfo.h"
#include "system/Startup.h"
#include "engine/ReplayEngine.h"
#include "ui/MainWindow.h"

#include <mfapi.h>
#include <winrt/base.h>
#include <shellapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "windowsapp.lib")

using namespace clipback;

namespace {

bool HasArg(const wchar_t* name)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool found = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) if (_wcsicmp(argv[i], name) == 0) found = true;
        LocalFree(argv);
    }
    return found;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Single instance: a second launch just shows the existing window.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ClipBack.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(L"ClipBackMainWindow", nullptr)) {
            ShowWindow(existing, SW_SHOWNORMAL);
            SetForegroundWindow(existing);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    // Per-monitor DPI awareness is declared in the manifest; this is the runtime fallback.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    logging::Initialize(paths::LogDirectory());
    LOG_INFO(L"==== ClipBack starting (%s) ====", paths::ExePath().c_str());

    // The UI thread is an STA (shell dialogs, tray); WinRT capture objects are agile so this is fine.
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        LOG_HR(L"MFStartup failed", hr);
        MessageBoxW(nullptr, (L"Media Foundation could not be initialised.\n" + logging::HResultToString(hr) +
            L"\n\nOn Windows N/KN editions install the 'Media Feature Pack'.").c_str(), L"ClipBack", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Keep the process off the "high" power/timer path: the pipeline is event driven and needs no timer boost.
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);

    int exitCode = 0;
    {
        Settings settings;
        const std::wstring configPath = paths::ConfigFilePath();
        const bool hadConfig = LoadSettings(configPath, settings);
        settings.startWithWindows = startup::IsEnabled();
        if (!hadConfig) {
            std::wstring err;
            if (!SaveSettings(configPath, settings, err)) LOG_WARN(L"Could not write default config: %s", err.c_str());
        }
        LOG_INFO(L"Config: %s (%s)", configPath.c_str(), hadConfig ? L"loaded" : L"defaults");

        SystemInfo sysInfo = CollectSystemInfo();
        LOG_INFO(L"%s", sysInfo.ToReport().c_str());

        ReplayEngine engine;
        MainWindow window(engine, settings, sysInfo);
        const bool startHidden = hadConfig && (settings.startMinimized || HasArg(L"--tray"));
        if (!window.Create(instance, startHidden)) {
            LOG_ERROR(L"Main window creation failed");
            MessageBoxW(nullptr, L"The main window could not be created.", L"ClipBack", MB_ICONERROR | MB_OK);
            exitCode = 1;
        } else {
            if (settings.autoStartReplay) window.StartReplay(true);

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (window.TranslateAccelerators(msg)) continue;
                if (!IsDialogMessageW(window.Handle(), &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            exitCode = static_cast<int>(msg.wParam);
        }
        engine.Stop();
    }

    MFShutdown();
    winrt::uninit_apartment();
    LOG_INFO(L"==== ClipBack exited (%d) ====", exitCode);
    logging::Shutdown();
    if (mutex) CloseHandle(mutex);
    return exitCode;
}
