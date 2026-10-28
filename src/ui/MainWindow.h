// Native Win32 main window: Replay / Settings / Diagnostics tabs, tray icon, hotkey owner.
#pragma once
#include "common/Common.h"
#include "settings/Settings.h"
#include "system/SystemInfo.h"
#include "engine/ReplayEngine.h"
#include "hotkeys/HotkeyManager.h"
#include "ui/TrayIcon.h"
#include <vector>
#include <string>

namespace clipback {

class MainWindow {
public:
    static constexpr UINT kEngineEventMessage = WM_APP + 2;
    static constexpr int kMatchHotkeyId = 2;   // Ctrl+9: start / stop recording the whole match

    MainWindow(ReplayEngine& engine, Settings& settings, SystemInfo& sysInfo);
    ~MainWindow();

    bool Create(HINSTANCE instance, bool startHidden);
    HWND Handle() const { return hwnd_; }
    void Show();
    void ShowSettings();
    bool TranslateAccelerators(MSG& msg);   // call from the message loop
    bool StartReplay(bool notify);
    void StopReplay();
    void SaveReplay();
    void ToggleMatchRecording();

private:
    enum class Page { Replay = 0, Settings = 1, Diagnostics = 2 };
    enum { kStatusTimer = 1, kRestartTimer = 2, kMatchRetryTimer = 3 };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void CreateFonts();
    void CreateControls();
    void CreateReplayPage();
    void CreateSettingsPage();
    void CreateDiagnosticsPage();
    void Layout();
    void SelectPage(Page page);
    void PaintHeader(HDC dc);
    int Scale(int v) const { return MulDiv(v, static_cast<int>(dpi_), 96); }

    HWND MakeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0);
    HWND MakeLabel(const wchar_t* text, int id);
    void SetInfo(int row, const std::wstring& value);

    void RefreshStatus();
    void LoadSettingsIntoControls();
    bool ReadSettingsFromControls(Settings& out, std::wstring& error);
    void ApplySettings();
    void BrowseForFolder();
    void RefreshDiagnostics();
    void OpenSaveFolder();
    void RegisterHotkeyOrWarn();
    void OnEngineEvent(const EngineEvent& e);
    void OnTrayMessage(WPARAM wp, LPARAM lp);
    void ShowTrayMenu();
    void OnCommand(int id, int code, HWND control);
    void UpdateBitrateEditState();
    void UpdateCaptureExclusion();
    void OpenCreditLink();
    void RebuildControls();
    void Exit();
    void ScheduleMatchStart();
    void TryStartMatch();
    static std::wstring MatchHotkeyText() { return L"Ctrl+9"; }

    ReplayEngine& engine_;
    Settings& settings_;
    SystemInfo& sysInfo_;
    HotkeyManager hotkeys_;
    TrayIcon tray_;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND tabs_ = nullptr;
    HACCEL accelerators_ = nullptr;
    UINT dpi_ = 96;
    HFONT fontNormal_ = nullptr;
    HFONT fontBold_ = nullptr;
    HFONT fontTitle_ = nullptr;
    HFONT fontHeader_ = nullptr;
    HFONT fontMono_ = nullptr;
    HFONT fontLink_ = nullptr;
    HBRUSH pageBrush_ = nullptr;
    HBRUSH accentBrush_ = nullptr;
    HWND creditLink_ = nullptr;
    HICON iconLarge_ = nullptr;
    HICON iconSmall_ = nullptr;
    Page page_ = Page::Replay;
    bool exiting_ = false;
    int restartAttempts_ = 0;
    std::wstring lastTooltip_;
    bool matchWanted_ = false;        // the user asked for a whole-match recording
    bool matchRetryPending_ = false;
    int matchRetries_ = 0;
    bool matchHotkeyOk_ = false;

    struct InfoRow { HWND label; HWND value; };
    std::vector<InfoRow> infoRows_;
    std::vector<HWND> replayControls_, settingsControls_, diagControls_;
    std::vector<HWND> settingsLabels_;

    // Settings page state
    std::vector<AudioDeviceInfo> micDevices_, outputDevices_;
    HWND statusHeader_ = nullptr, statusSub_ = nullptr;
    HWND btnToggle_ = nullptr, btnSave_ = nullptr, btnOpenFolder_ = nullptr, btnMatch_ = nullptr;
    HWND cmbLanguage_ = nullptr;
    HWND cmbDuration_ = nullptr, cmbResolution_ = nullptr, cmbFps_ = nullptr, cmbBitrate_ = nullptr, edtBitrate_ = nullptr;
    HWND cmbEncoder_ = nullptr, cmbMonitor_ = nullptr, hotkeyCtl_ = nullptr, chkCursor_ = nullptr;
    HWND chkMic_ = nullptr, cmbMic_ = nullptr, cmbOutput_ = nullptr, edtSaveDir_ = nullptr, btnBrowse_ = nullptr;
    HWND chkStartup_ = nullptr, chkMinimized_ = nullptr, chkAutoStart_ = nullptr, btnApply_ = nullptr, btnDefaults_ = nullptr;
    HWND diagText_ = nullptr, btnDiagRefresh_ = nullptr, btnDiagCopy_ = nullptr, btnDiagLog_ = nullptr;
};

} // namespace clipback
