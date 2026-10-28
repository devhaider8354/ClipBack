#include "ui/MainWindow.h"
#include "ui/resource.h"
#include "ui/Strings.h"
#include "system/Paths.h"
#include "system/Startup.h"
#include "logging/Logger.h"
#include "common/StringUtils.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")
#include <iterator>

namespace clipback {

namespace {

constexpr wchar_t kClassName[] = L"ClipBackMainWindow";
constexpr int kClientWidth = 640;
constexpr int kClientHeight = 720;
constexpr int kHeaderHeight = 68;
constexpr COLORREF kAccent = RGB(0, 120, 212);
constexpr COLORREF kAccentDark = RGB(0, 90, 158);
constexpr COLORREF kPageBg = RGB(255, 255, 255);
constexpr COLORREF kTextMuted = RGB(96, 96, 96);
constexpr COLORREF kGreen = RGB(16, 124, 16);

enum InfoRowIndex { RowState, RowDuration, RowResolution, RowFps, RowBitrate, RowEncoder, RowCapture, RowAudio, RowHotkey, RowFolder, RowBuffered, RowMemory, RowLastSaved, RowMatch, RowCount };
const Str kInfoLabels[RowCount] = {
    Str::LblStatus, Str::LblDuration, Str::LblResolution, Str::LblFrameRate, Str::LblBitrate, Str::LblEncoder, Str::LblCapture,
    Str::LblAudio, Str::LblHotkey, Str::LblSaveLocation, Str::LblBuffered, Str::LblBufferMemory, Str::LblLastSaved, Str::LblMatch
};

std::wstring FormatClock(double seconds)
{
    const long long t = static_cast<long long>(seconds);
    return t >= 3600 ? str::Format(L"%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60) : str::Format(L"%02lld:%02lld", t / 60, t % 60);
}

const int kBitratePresets[] = { 8000, 12000, 16000, 20000 };

} // namespace

MainWindow::MainWindow(ReplayEngine& engine, Settings& settings, SystemInfo& sysInfo)
    : engine_(engine), settings_(settings), sysInfo_(sysInfo) {}

MainWindow::~MainWindow()
{
    tray_.Remove();
    if (accelerators_) DestroyAcceleratorTable(accelerators_);
    if (fontNormal_) DeleteObject(fontNormal_);
    if (fontBold_) DeleteObject(fontBold_);
    if (fontTitle_) DeleteObject(fontTitle_);
    if (fontHeader_) DeleteObject(fontHeader_);
    if (fontMono_) DeleteObject(fontMono_);
    if (fontLink_) DeleteObject(fontLink_);
    if (pageBrush_) DeleteObject(pageBrush_);
    if (accentBrush_) DeleteObject(accentBrush_);
}

// ---------------------------------------------------------------------------
// Creation
// ---------------------------------------------------------------------------

bool MainWindow::Create(HINSTANCE instance, bool startHidden)
{
    instance_ = instance;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    iconLarge_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    iconSmall_ = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!iconLarge_) iconLarge_ = LoadIconW(nullptr, IDI_APPLICATION);
    if (!iconSmall_) iconSmall_ = iconLarge_;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &MainWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hIcon = iconLarge_;
    wc.hIconSm = iconSmall_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kPageBg);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    pageBrush_ = CreateSolidBrush(kPageBg);
    accentBrush_ = CreateSolidBrush(kAccent);

    SetLanguage(settings_.language);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    const DWORD exStyle = IsRtl() ? (WS_EX_LAYOUTRTL | WS_EX_RTLREADING) : 0;
    hwnd_ = CreateWindowExW(exStyle, kClassName, L"ClipBack", style, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;


    // Rounded corners on Windows 11 (ignored elsewhere).
    const int preference = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &preference, sizeof(preference));

    if (!tray_.Add(hwnd_, iconSmall_, L"ClipBack")) LOG_WARN(L"Tray icon could not be added");
    RegisterHotkeyOrWarn();
    matchHotkeyOk_ = RegisterHotKey(hwnd_, kMatchHotkeyId, MOD_CONTROL | MOD_NOREPEAT, '9') != FALSE;
    if (!matchHotkeyOk_) LOG_WARN(L"Match recording hotkey Ctrl+9 is used by another application");

    engine_.SetEventCallback([this](const EngineEvent& e) {
        // Worker thread -> UI thread hand-off; the UI thread frees the copy.
        PostMessageW(hwnd_, kEngineEventMessage, 0, reinterpret_cast<LPARAM>(new EngineEvent(e)));
    });

    // Keyboard shortcuts: Ctrl+Q exits, Ctrl+S saves a replay, Ctrl+O opens the replay folder.
    static const ACCEL accels[] = {
        { FCONTROL | FVIRTKEY, 'Q', IDM_TRAY_EXIT },
        { FCONTROL | FVIRTKEY, 'S', IDC_BTN_SAVE },
        { FCONTROL | FVIRTKEY, 'O', IDC_BTN_OPEN_FOLDER },
    };
    accelerators_ = CreateAcceleratorTableW(const_cast<ACCEL*>(accels), static_cast<int>(std::size(accels)));

    if (!startHidden) Show();
    SetTimer(hwnd_, kStatusTimer, 1000, nullptr);
    return true;
}

bool MainWindow::TranslateAccelerators(MSG& msg)
{
    return accelerators_ && TranslateAcceleratorW(hwnd_, accelerators_, &msg) != 0;
}

void MainWindow::Show()
{
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    RefreshStatus();
}

void MainWindow::ShowSettings()
{
    Show();
    TabCtrl_SetCurSel(tabs_, static_cast<int>(Page::Settings));
    SelectPage(Page::Settings);
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

void MainWindow::CreateFonts()
{
    if (fontNormal_) DeleteObject(fontNormal_);
    if (fontBold_) DeleteObject(fontBold_);
    if (fontTitle_) DeleteObject(fontTitle_);
    if (fontHeader_) DeleteObject(fontHeader_);
    if (fontMono_) DeleteObject(fontMono_);
    auto make = [&](int pt, int weight, const wchar_t* face) {
        return CreateFontW(-MulDiv(pt, static_cast<int>(dpi_), 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    };
    fontNormal_ = make(10, FW_NORMAL, L"Segoe UI");
    fontBold_ = make(10, FW_SEMIBOLD, L"Segoe UI");
    fontTitle_ = make(16, FW_SEMIBOLD, L"Segoe UI");
    fontHeader_ = make(18, FW_SEMIBOLD, L"Segoe UI");
    fontMono_ = make(9, FW_NORMAL, L"Consolas");
    if (fontLink_) DeleteObject(fontLink_);
    fontLink_ = CreateFontW(-MulDiv(10, static_cast<int>(dpi_), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, TRUE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

HWND MainWindow::MakeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle)
{
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, 0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fontNormal_), TRUE);
    return h;
}

HWND MainWindow::MakeLabel(const wchar_t* text, int id)
{
    return MakeControl(L"STATIC", text, SS_LEFT | SS_NOPREFIX, id);
}

void MainWindow::CreateControls()
{
    tabs_ = MakeControl(WC_TABCONTROLW, L"", WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS, IDC_TABS);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(T(Str::TabReplay));
    TabCtrl_InsertItem(tabs_, 0, &item);
    item.pszText = const_cast<wchar_t*>(T(Str::TabSettings));
    TabCtrl_InsertItem(tabs_, 1, &item);
    item.pszText = const_cast<wchar_t*>(T(Str::TabDiagnostics));
    TabCtrl_InsertItem(tabs_, 2, &item);

    // Credit link in the header ("by Haider" -> bio.byhdr.com/king)
    creditLink_ = MakeControl(L"STATIC", T(Str::CreditLink), WS_VISIBLE | SS_RIGHT | SS_NOTIFY | SS_NOPREFIX, IDC_CREDIT_LINK);
    SendMessageW(creditLink_, WM_SETFONT, reinterpret_cast<WPARAM>(fontLink_), TRUE);

    CreateReplayPage();
    CreateSettingsPage();
    CreateDiagnosticsPage();
    LoadSettingsIntoControls();
    RefreshDiagnostics();
    SelectPage(Page::Replay);
}

void MainWindow::CreateReplayPage()
{
    statusHeader_ = MakeControl(L"STATIC", T(Str::StatusStopped), SS_LEFT | SS_NOPREFIX, IDC_STATUS_HEADER);
    SendMessageW(statusHeader_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHeader_), TRUE);
    statusSub_ = MakeControl(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, IDC_STATUS_SUB);
    replayControls_ = { statusHeader_, statusSub_ };

    for (int i = 0; i < RowCount; ++i) {
        InfoRow row;
        row.label = MakeLabel(T(kInfoLabels[i]), IDC_INFO_FIRST + i * 2);
        row.value = MakeControl(L"STATIC", L"-", SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, IDC_INFO_FIRST + i * 2 + 1);
        SendMessageW(row.value, WM_SETFONT, reinterpret_cast<WPARAM>(fontBold_), TRUE);
        infoRows_.push_back(row);
        replayControls_.push_back(row.label);
        replayControls_.push_back(row.value);
    }

    btnToggle_ = MakeControl(L"BUTTON", T(Str::BtnStartReplay), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_TOGGLE);
    btnSave_ = MakeControl(L"BUTTON", T(Str::BtnSaveReplay), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_SAVE);
    btnOpenFolder_ = MakeControl(L"BUTTON", T(Str::BtnOpenFolder), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_OPEN_FOLDER);
    btnMatch_ = MakeControl(L"BUTTON", str::Format(T(Str::BtnStartMatch), MatchHotkeyText().c_str()).c_str(), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_MATCH);
    replayControls_.insert(replayControls_.end(), { btnToggle_, btnSave_, btnOpenFolder_, btnMatch_ });
}

void MainWindow::CreateSettingsPage()
{
    auto label = [&](const wchar_t* text) {
        HWND h = MakeLabel(text, IDC_SETTINGS_LABEL_FIRST + static_cast<int>(settingsLabels_.size()));
        settingsLabels_.push_back(h);
        settingsControls_.push_back(h);
        return h;
    };
    auto combo = [&](int id) {
        HWND h = MakeControl(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, id);
        settingsControls_.push_back(h);
        return h;
    };

    label(T(Str::SetLanguage));
    cmbLanguage_ = combo(IDC_CMB_LANGUAGE);
    ComboBox_AddString(cmbLanguage_, T(Str::LangEnglish));
    ComboBox_AddString(cmbLanguage_, T(Str::LangArabic));

    label(T(Str::SetDuration));
    cmbDuration_ = combo(IDC_CMB_DURATION);
    ComboBox_AddString(cmbDuration_, T(Str::Dur30));
    ComboBox_AddString(cmbDuration_, T(Str::Dur60));
    ComboBox_AddString(cmbDuration_, T(Str::Dur120));

    label(T(Str::SetResolution));
    cmbResolution_ = combo(IDC_CMB_RESOLUTION);
    ComboBox_AddString(cmbResolution_, T(Str::ResSame));
    ComboBox_AddString(cmbResolution_, T(Str::Res1080));
    ComboBox_AddString(cmbResolution_, T(Str::Res720));

    label(T(Str::SetFrameRate));
    cmbFps_ = combo(IDC_CMB_FPS);
    ComboBox_AddString(cmbFps_, T(Str::Fps60));
    ComboBox_AddString(cmbFps_, T(Str::Fps30));

    label(T(Str::SetBitrate));
    cmbBitrate_ = combo(IDC_CMB_BITRATE);
    ComboBox_AddString(cmbBitrate_, T(Str::Br8));
    ComboBox_AddString(cmbBitrate_, T(Str::Br12));
    ComboBox_AddString(cmbBitrate_, T(Str::Br16));
    ComboBox_AddString(cmbBitrate_, T(Str::Br20));
    ComboBox_AddString(cmbBitrate_, T(Str::BrCustom));
    edtBitrate_ = MakeControl(L"EDIT", L"12000", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, IDC_EDT_BITRATE, WS_EX_CLIENTEDGE);
    settingsControls_.push_back(edtBitrate_);

    label(T(Str::SetEncoder));
    cmbEncoder_ = combo(IDC_CMB_ENCODER);
    ComboBox_AddString(cmbEncoder_, T(Str::EncAuto));
    ComboBox_AddString(cmbEncoder_, T(Str::EncNvenc));
    ComboBox_AddString(cmbEncoder_, T(Str::EncAmf));
    ComboBox_AddString(cmbEncoder_, T(Str::EncQsv));
    ComboBox_AddString(cmbEncoder_, T(Str::EncCpu));

    label(T(Str::SetDisplay));
    cmbMonitor_ = combo(IDC_CMB_MONITOR);

    label(T(Str::SetHotkey));
    hotkeyCtl_ = MakeControl(HOTKEY_CLASSW, L"", WS_TABSTOP, IDC_HOTKEY, WS_EX_CLIENTEDGE);
    settingsControls_.push_back(hotkeyCtl_);

    label(T(Str::SetSaveFolder));
    edtSaveDir_ = MakeControl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_EDT_SAVEDIR, WS_EX_CLIENTEDGE);
    // File paths stay left-to-right even in the Arabic (mirrored) UI.
    if (IsRtl()) SetWindowLongPtrW(edtSaveDir_, GWL_EXSTYLE, GetWindowLongPtrW(edtSaveDir_, GWL_EXSTYLE) & ~(WS_EX_LAYOUTRTL | WS_EX_RTLREADING));
    btnBrowse_ = MakeControl(L"BUTTON", T(Str::BtnBrowse), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_BROWSE);
    settingsControls_.insert(settingsControls_.end(), { edtSaveDir_, btnBrowse_ });

    label(T(Str::SetOutputDevice));
    cmbOutput_ = combo(IDC_CMB_OUTPUT);

    label(T(Str::SetMicrophone));
    chkMic_ = MakeControl(L"BUTTON", T(Str::ChkRecordMic), WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_MIC);
    cmbMic_ = combo(IDC_CMB_MIC);
    settingsControls_.push_back(chkMic_);

    chkCursor_ = MakeControl(L"BUTTON", T(Str::ChkCursor), WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_CURSOR);
    chkStartup_ = MakeControl(L"BUTTON", T(Str::ChkStartup), WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_STARTUP);
    chkMinimized_ = MakeControl(L"BUTTON", T(Str::ChkMinimized), WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_MINIMIZED);
    chkAutoStart_ = MakeControl(L"BUTTON", T(Str::ChkAutoStart), WS_TABSTOP | BS_AUTOCHECKBOX, IDC_CHK_AUTOSTART);
    btnApply_ = MakeControl(L"BUTTON", T(Str::BtnApply), WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_BTN_APPLY);
    btnDefaults_ = MakeControl(L"BUTTON", T(Str::BtnDefaults), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_DEFAULTS);
    settingsControls_.insert(settingsControls_.end(), { chkCursor_, chkStartup_, chkMinimized_, chkAutoStart_, btnApply_, btnDefaults_ });
}

void MainWindow::CreateDiagnosticsPage()
{
    diagText_ = MakeControl(L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, IDC_DIAG_TEXT, WS_EX_CLIENTEDGE);
    SendMessageW(diagText_, WM_SETFONT, reinterpret_cast<WPARAM>(fontMono_), TRUE);
    btnDiagRefresh_ = MakeControl(L"BUTTON", T(Str::BtnRefresh), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_DIAG_REFRESH);
    btnDiagCopy_ = MakeControl(L"BUTTON", T(Str::BtnCopy), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_DIAG_COPY);
    btnDiagLog_ = MakeControl(L"BUTTON", T(Str::BtnOpenLog), WS_TABSTOP | BS_PUSHBUTTON, IDC_BTN_DIAG_LOG);
    diagControls_ = { diagText_, btnDiagRefresh_, btnDiagCopy_, btnDiagLog_ };
}

// ---------------------------------------------------------------------------
// Layout (DPI aware)
// ---------------------------------------------------------------------------

void MainWindow::Layout()
{
    RECT client{ 0, 0, Scale(kClientWidth), Scale(kClientHeight) };
    RECT wr = client;
    AdjustWindowRectExForDpi(&wr, static_cast<DWORD>(GetWindowLongW(hwnd_, GWL_STYLE)), FALSE, 0, dpi_);
    SetWindowPos(hwnd_, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    const int margin = Scale(16);
    const int tabTop = Scale(kHeaderHeight) + Scale(12);
    SetWindowPos(tabs_, nullptr, margin, tabTop, client.right - 2 * margin, client.bottom - tabTop - margin, SWP_NOZORDER);

    SetWindowPos(creditLink_, nullptr, client.right - Scale(200) - Scale(20), Scale(38), Scale(200), Scale(22), SWP_NOZORDER);

    RECT page;
    GetWindowRect(tabs_, &page);
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&page), 2);
    TabCtrl_AdjustRect(tabs_, FALSE, &page);
    const int px = page.left + Scale(16), py = page.top + Scale(12);
    const int pw = page.right - page.left - Scale(32);
    const int ph = page.bottom - page.top - Scale(24);

    // ---- Replay page
    {
        int y = py;
        SetWindowPos(statusHeader_, nullptr, px, y, pw, Scale(34), SWP_NOZORDER);
        y += Scale(36);
        SetWindowPos(statusSub_, nullptr, px, y, pw, Scale(20), SWP_NOZORDER);
        y += Scale(34);
        const int labelW = Scale(150), rowH = Scale(26);
        for (auto& row : infoRows_) {
            SetWindowPos(row.label, nullptr, px, y + Scale(3), labelW, rowH, SWP_NOZORDER);
            SetWindowPos(row.value, nullptr, px + labelW, y + Scale(3), pw - labelW, rowH, SWP_NOZORDER);
            y += rowH;
        }
        y += Scale(18);
        const int bw = Scale(160), bh = Scale(36), gap = Scale(12);
        SetWindowPos(btnToggle_, nullptr, px, y, bw, bh, SWP_NOZORDER);
        SetWindowPos(btnSave_, nullptr, px + bw + gap, y, bw, bh, SWP_NOZORDER);
        SetWindowPos(btnOpenFolder_, nullptr, px + 2 * (bw + gap), y, bw, bh, SWP_NOZORDER);
        y += bh + gap;
        SetWindowPos(btnMatch_, nullptr, px, y, 2 * bw + gap, bh, SWP_NOZORDER);
    }

    // ---- Settings page
    {
        int y = py;
        const int labelW = Scale(170), rowH = Scale(34), ctlH = Scale(26);
        const int cx = px + labelW, cw = pw - labelW;
        size_t li = 0;
        auto placeLabel = [&](int rowY) { SetWindowPos(settingsLabels_[li++], nullptr, px, rowY + Scale(4), labelW, ctlH, SWP_NOZORDER); };
        auto place = [&](HWND h, int x, int w) { SetWindowPos(h, nullptr, x, y, w, Scale(200), SWP_NOZORDER); };

        placeLabel(y); place(cmbLanguage_, cx, cw); y += rowH;
        placeLabel(y); place(cmbDuration_, cx, cw); y += rowH;
        placeLabel(y); place(cmbResolution_, cx, cw); y += rowH;
        placeLabel(y); place(cmbFps_, cx, cw); y += rowH;
        placeLabel(y); place(cmbBitrate_, cx, cw - Scale(130)); SetWindowPos(edtBitrate_, nullptr, cx + cw - Scale(120), y, Scale(120), ctlH, SWP_NOZORDER); y += rowH;
        placeLabel(y); place(cmbEncoder_, cx, cw); y += rowH;
        placeLabel(y); place(cmbMonitor_, cx, cw); y += rowH;
        placeLabel(y); SetWindowPos(hotkeyCtl_, nullptr, cx, y, Scale(200), ctlH, SWP_NOZORDER); y += rowH;
        placeLabel(y); SetWindowPos(edtSaveDir_, nullptr, cx, y, cw - Scale(110), ctlH, SWP_NOZORDER);
        SetWindowPos(btnBrowse_, nullptr, cx + cw - Scale(100), y - Scale(1), Scale(100), ctlH + Scale(2), SWP_NOZORDER); y += rowH;
        placeLabel(y); place(cmbOutput_, cx, cw); y += rowH;
        placeLabel(y); SetWindowPos(chkMic_, nullptr, cx, y + Scale(2), Scale(170), ctlH, SWP_NOZORDER);
        SetWindowPos(cmbMic_, nullptr, cx + Scale(180), y, cw - Scale(180), Scale(200), SWP_NOZORDER); y += rowH + Scale(6);

        const int half = pw / 2;
        SetWindowPos(chkCursor_, nullptr, px, y, half, ctlH, SWP_NOZORDER);
        SetWindowPos(chkStartup_, nullptr, px + half, y, half, ctlH, SWP_NOZORDER); y += Scale(30);
        SetWindowPos(chkMinimized_, nullptr, px, y, half, ctlH, SWP_NOZORDER);
        SetWindowPos(chkAutoStart_, nullptr, px + half, y, half, ctlH, SWP_NOZORDER); y += Scale(40);

        SetWindowPos(btnApply_, nullptr, px, y, Scale(160), Scale(36), SWP_NOZORDER);
        SetWindowPos(btnDefaults_, nullptr, px + Scale(172), y, Scale(160), Scale(36), SWP_NOZORDER);
    }

    // ---- Diagnostics page
    {
        const int bh = Scale(32);
        SetWindowPos(diagText_, nullptr, px, py, pw, ph - bh - Scale(12), SWP_NOZORDER);
        const int by = py + ph - bh;
        SetWindowPos(btnDiagRefresh_, nullptr, px, by, Scale(120), bh, SWP_NOZORDER);
        SetWindowPos(btnDiagCopy_, nullptr, px + Scale(132), by, Scale(150), bh, SWP_NOZORDER);
        SetWindowPos(btnDiagLog_, nullptr, px + Scale(294), by, Scale(130), bh, SWP_NOZORDER);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void MainWindow::SelectPage(Page page)
{
    page_ = page;
    auto show = [](const std::vector<HWND>& list, bool visible) {
        for (HWND h : list) ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
    };
    show(replayControls_, page == Page::Replay);
    show(settingsControls_, page == Page::Settings);
    show(diagControls_, page == Page::Diagnostics);
    if (page == Page::Settings) UpdateBitrateEditState();
    if (page == Page::Replay) RefreshStatus();
}

void MainWindow::PaintHeader(HDC dc)
{
    RECT r{ 0, 0, Scale(kClientWidth), Scale(kHeaderHeight) };
    FillRect(dc, &r, accentBrush_);
    // thin darker rule under the header
    RECT rule{ r.left, r.bottom - Scale(3), r.right, r.bottom };
    HBRUSH dark = CreateSolidBrush(kAccentDark);
    FillRect(dc, &rule, dark);
    DeleteObject(dark);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HGDIOBJ old = SelectObject(dc, fontTitle_);
    RECT tr{ Scale(20), Scale(12), r.right - Scale(20), Scale(42) };
    DrawTextW(dc, T(Str::AppTitle), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, fontNormal_);
    RECT sr{ Scale(20), Scale(40), r.right - Scale(20), Scale(62) };
    DrawTextW(dc, T(Str::Tagline), -1, &sr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, old);
}

// ---------------------------------------------------------------------------
// Status page
// ---------------------------------------------------------------------------

void MainWindow::SetInfo(int row, const std::wstring& value)
{
    if (row < 0 || row >= static_cast<int>(infoRows_.size())) return;
    wchar_t current[512] = {};
    GetWindowTextW(infoRows_[static_cast<size_t>(row)].value, current, 512);
    if (value != current) SetWindowTextW(infoRows_[static_cast<size_t>(row)].value, value.c_str());
}

static std::wstring ResolutionText(ResolutionMode m)
{
    switch (m) {
    case ResolutionMode::R1080p: return T(Str::Res1080);
    case ResolutionMode::R720p: return T(Str::Res720);
    default: return T(Str::ValSameAsGame);
    }
}

static std::wstring EncoderText(EncoderPreference e)
{
    switch (e) {
    case EncoderPreference::Nvenc: return T(Str::EncNvenc);
    case EncoderPreference::Amf: return T(Str::EncAmf);
    case EncoderPreference::Qsv: return T(Str::EncQsv);
    case EncoderPreference::Cpu: return T(Str::EncCpu);
    default: return T(Str::EncAuto);
    }
}

void MainWindow::RefreshStatus()
{
    const EngineStatus s = engine_.Status();
    const bool running = s.running;

    std::wstring header = T(running ? Str::StatusRecording : Str::StatusStopped);
    wchar_t current[64] = {};
    GetWindowTextW(statusHeader_, current, 64);
    if (header != current) SetWindowTextW(statusHeader_, header.c_str());

    std::wstring sub = running
        ? str::Format(T(Str::SubRecording), s.replayDurationSec, settings_.hotkey.ToString().c_str())
        : T(Str::SubStopped);
    if (s.saving) sub = T(Str::SubSaving);
    SetWindowTextW(statusSub_, sub.c_str());

    SetInfo(RowState, T(running ? (s.saving ? Str::ValRecordingSaving : Str::ValRecording) : Str::ValStopped));
    SetInfo(RowDuration, str::Format(T(Str::ValSeconds), running ? s.replayDurationSec : settings_.replayDurationSec));
    SetInfo(RowResolution, running && s.width ? str::Format(L"%u x %u", s.width, s.height) : ResolutionText(settings_.resolution));
    SetInfo(RowFps, str::Format(T(Str::ValFps), running ? s.fps : static_cast<UINT>(settings_.fps)));
    SetInfo(RowBitrate, str::Format(T(Str::ValMbps), (running ? s.bitrateKbps : static_cast<UINT>(settings_.bitrateKbps)) / 1000.0));
    SetInfo(RowEncoder, running ? s.encoderName + T(s.hardwareEncoder ? Str::ValHardware : Str::ValSoftware) : EncoderText(settings_.encoder));
    SetInfo(RowCapture, running ? s.captureName : L"-");
    SetInfo(RowAudio, T(running ? (s.audioActive ? (s.microphoneActive ? Str::ValGameAudioMic : Str::ValGameAudio) : Str::ValUnavailable) : (settings_.microphoneEnabled ? Str::ValGameAudioMic : Str::ValGameAudio)));
    SetInfo(RowHotkey, settings_.hotkey.ToString() + (hotkeys_.IsRegistered() ? L"" : T(Str::ValHotkeyNotRegistered)));
    SetInfo(RowFolder, settings_.EffectiveSaveDirectory());
    SetInfo(RowBuffered, running ? str::Format(T(Str::ValBuffered), s.bufferedSeconds, static_cast<unsigned long long>(s.framesEncoded), static_cast<unsigned long long>(s.framesDropped)) : L"-");
    SetInfo(RowMemory, running ? str::BytesToHuman(s.bufferBytes) : L"-");
    SetInfo(RowLastSaved, s.lastSavedFile.empty() ? L"-" : s.lastSavedFile);
    SetInfo(RowMatch, s.matchRecording
        ? str::Format(T(Str::ValMatchOn), FormatClock(s.matchSeconds).c_str(), str::BytesToHuman(s.matchBytes).c_str())
        : str::Format(T(Str::ValMatchOff), MatchHotkeyText().c_str()));
    SetWindowTextW(btnMatch_, s.matchRecording ? T(Str::BtnStopMatch) : str::Format(T(Str::BtnStartMatch), MatchHotkeyText().c_str()).c_str());

    SetWindowTextW(btnToggle_, T(running ? Str::BtnStopReplay : Str::BtnStartReplay));
    EnableWindow(btnSave_, running && !s.saving);
    const std::wstring tip = T(running ? Str::TipRecording : Str::TipStopped);
    if (tip != lastTooltip_) { lastTooltip_ = tip; tray_.SetTooltip(tip); }
}

// ---------------------------------------------------------------------------
// Settings page
// ---------------------------------------------------------------------------

static void FillDeviceCombo(HWND combo, const std::vector<AudioDeviceInfo>& devices, const std::wstring& selectedId)
{
    ComboBox_ResetContent(combo);
    ComboBox_AddString(combo, T(Str::DefaultDevice));
    int sel = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        ComboBox_AddString(combo, devices[i].name.c_str());
        if (!selectedId.empty() && devices[i].id == selectedId) sel = static_cast<int>(i) + 1;
    }
    ComboBox_SetCurSel(combo, sel);
}

void MainWindow::LoadSettingsIntoControls()
{
    const Settings& s = settings_;
    ComboBox_SetCurSel(cmbLanguage_, static_cast<int>(s.language));
    ComboBox_SetCurSel(cmbDuration_, s.replayDurationSec <= 30 ? 0 : (s.replayDurationSec >= 120 ? 2 : 1));
    ComboBox_SetCurSel(cmbResolution_, static_cast<int>(s.resolution));
    ComboBox_SetCurSel(cmbFps_, s.fps == 30 ? 1 : 0);
    int bitrateIndex = 4;
    for (int i = 0; i < 4; ++i) if (kBitratePresets[i] == s.bitrateKbps) bitrateIndex = i;
    ComboBox_SetCurSel(cmbBitrate_, bitrateIndex);
    SetWindowTextW(edtBitrate_, std::to_wstring(s.bitrateKbps).c_str());
    ComboBox_SetCurSel(cmbEncoder_, static_cast<int>(s.encoder));

    ComboBox_ResetContent(cmbMonitor_);
    for (size_t i = 0; i < sysInfo_.monitors.size(); ++i) {
        const auto& m = sysInfo_.monitors[i];
        ComboBox_AddString(cmbMonitor_, (str::Format(T(Str::DisplayItem), static_cast<unsigned>(i + 1), m.width, m.height, m.refreshHz) + (m.primary ? T(Str::DisplayPrimary) : L"")).c_str());
    }
    if (sysInfo_.monitors.empty()) ComboBox_AddString(cmbMonitor_, T(Str::DisplayPrimaryOnly));
    ComboBox_SetCurSel(cmbMonitor_, s.monitorIndex < ComboBox_GetCount(cmbMonitor_) ? s.monitorIndex : 0);

    SendMessageW(hotkeyCtl_, HKM_SETHOTKEY, HotkeyManager::ToControlValue(s.hotkey), 0);
    Button_SetCheck(chkCursor_, s.captureCursor ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkMic_, s.microphoneEnabled ? BST_CHECKED : BST_UNCHECKED);

    micDevices_ = sysInfo_.captureDevices;
    outputDevices_ = sysInfo_.renderDevices;
    FillDeviceCombo(cmbMic_, micDevices_, s.microphoneDeviceId);
    FillDeviceCombo(cmbOutput_, outputDevices_, s.outputDeviceId);
    EnableWindow(cmbMic_, s.microphoneEnabled);

    SetWindowTextW(edtSaveDir_, s.saveDirectory.empty() ? paths::DefaultSaveDirectory().c_str() : s.saveDirectory.c_str());
    Button_SetCheck(chkStartup_, s.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkMinimized_, s.startMinimized ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(chkAutoStart_, s.autoStartReplay ? BST_CHECKED : BST_UNCHECKED);
    UpdateBitrateEditState();
}

void MainWindow::UpdateBitrateEditState()
{
    const int idx = ComboBox_GetCurSel(cmbBitrate_);
    if (idx >= 0 && idx < 4) SetWindowTextW(edtBitrate_, std::to_wstring(kBitratePresets[idx]).c_str());
    EnableWindow(edtBitrate_, idx == 4);
}

bool MainWindow::ReadSettingsFromControls(Settings& out, std::wstring& error)
{
    out = settings_;
    out.language = ComboBox_GetCurSel(cmbLanguage_) == 1 ? Language::Arabic : Language::English;
    const int durationIdx = ComboBox_GetCurSel(cmbDuration_);
    out.replayDurationSec = durationIdx == 0 ? 30 : (durationIdx == 2 ? 120 : 60);
    out.resolution = static_cast<ResolutionMode>(std::max(0, ComboBox_GetCurSel(cmbResolution_)));
    out.fps = ComboBox_GetCurSel(cmbFps_) == 1 ? 30 : 60;

    const int bitrateIdx = ComboBox_GetCurSel(cmbBitrate_);
    if (bitrateIdx >= 0 && bitrateIdx < 4) {
        out.bitrateKbps = kBitratePresets[bitrateIdx];
    } else {
        wchar_t buf[32] = {};
        GetWindowTextW(edtBitrate_, buf, 32);
        const int v = _wtoi(buf);
        if (v < 1000 || v > 200000) { error = T(Str::MsgBitrateRange); return false; }
        out.bitrateKbps = v;
    }
    out.encoder = static_cast<EncoderPreference>(std::max(0, ComboBox_GetCurSel(cmbEncoder_)));
    out.monitorIndex = std::max(0, ComboBox_GetCurSel(cmbMonitor_));

    const WORD hk = static_cast<WORD>(SendMessageW(hotkeyCtl_, HKM_GETHOTKEY, 0, 0));
    if (LOBYTE(hk) == 0) { error = T(Str::MsgChooseHotkey); return false; }
    out.hotkey = HotkeyManager::FromControlValue(hk);

    out.captureCursor = Button_GetCheck(chkCursor_) == BST_CHECKED;
    out.microphoneEnabled = Button_GetCheck(chkMic_) == BST_CHECKED;
    const int micIdx = ComboBox_GetCurSel(cmbMic_);
    out.microphoneDeviceId = (micIdx > 0 && static_cast<size_t>(micIdx - 1) < micDevices_.size()) ? micDevices_[static_cast<size_t>(micIdx - 1)].id : L"";
    const int outIdx = ComboBox_GetCurSel(cmbOutput_);
    out.outputDeviceId = (outIdx > 0 && static_cast<size_t>(outIdx - 1) < outputDevices_.size()) ? outputDevices_[static_cast<size_t>(outIdx - 1)].id : L"";

    wchar_t dir[MAX_PATH * 2] = {};
    GetWindowTextW(edtSaveDir_, dir, static_cast<int>(std::size(dir)));
    std::wstring d = str::Trim(dir);
    out.saveDirectory = (d.empty() || d == paths::DefaultSaveDirectory()) ? L"" : d;
    if (!out.saveDirectory.empty() && !paths::EnsureDirectory(paths::Expand(out.saveDirectory))) {
        error = T(Str::MsgCannotCreateFolder) + out.saveDirectory;
        return false;
    }

    out.startWithWindows = Button_GetCheck(chkStartup_) == BST_CHECKED;
    out.startMinimized = Button_GetCheck(chkMinimized_) == BST_CHECKED;
    out.autoStartReplay = Button_GetCheck(chkAutoStart_) == BST_CHECKED;
    return true;
}

void MainWindow::ApplySettings()
{
    Settings updated;
    std::wstring error;
    if (!ReadSettingsFromControls(updated, error)) {
        MessageBoxW(hwnd_, error.c_str(), L"ClipBack", MB_ICONWARNING | MB_OK);
        return;
    }

    const bool hotkeyChanged = updated.hotkey != settings_.hotkey;
    const bool languageChanged = updated.language != settings_.language;
    const bool pipelineChanged =
        updated.replayDurationSec != settings_.replayDurationSec || updated.resolution != settings_.resolution || updated.fps != settings_.fps ||
        updated.bitrateKbps != settings_.bitrateKbps || updated.encoder != settings_.encoder || updated.monitorIndex != settings_.monitorIndex ||
        updated.captureCursor != settings_.captureCursor || updated.microphoneEnabled != settings_.microphoneEnabled ||
        updated.microphoneDeviceId != settings_.microphoneDeviceId || updated.outputDeviceId != settings_.outputDeviceId ||
        updated.saveDirectory != settings_.saveDirectory;

    if (updated.startWithWindows != startup::IsEnabled()) {
        std::wstring err;
        if (!startup::SetEnabled(updated.startWithWindows, err)) MessageBoxW(hwnd_, err.c_str(), L"ClipBack", MB_ICONWARNING | MB_OK);
    }

    settings_ = updated;
    if (!SaveSettings(paths::ConfigFilePath(), settings_, error)) {
        MessageBoxW(hwnd_, (T(Str::MsgSettingsWriteFailed) + error).c_str(), L"ClipBack", MB_ICONERROR | MB_OK);
    }
    LOG_INFO(L"Settings saved");

    if (hotkeyChanged) RegisterHotkeyOrWarn();

    if (pipelineChanged && engine_.IsRunning()) {
        engine_.Stop();
        StartReplay(false);
    }
    if (languageChanged) {
        SetLanguage(settings_.language);
        RebuildControls();
    }
    tray_.Notify(L"ClipBack", T(Str::MsgSettingsSaved), NIIF_INFO);
    RefreshStatus();
}

void MainWindow::BrowseForFolder()
{
    ComPtr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(T(Str::DlgChooseFolder));
    if (FAILED(dialog->Show(hwnd_))) return;
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(&item))) return;
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        SetWindowTextW(edtSaveDir_, path);
        CoTaskMemFree(path);
    }
}

void MainWindow::RegisterHotkeyOrWarn()
{
    std::wstring err;
    if (!hotkeys_.Register(hwnd_, settings_.hotkey, err)) {
        LOG_ERROR(L"%s", err.c_str());
        tray_.Notify(T(Str::MsgHotkeyProblem), err, NIIF_WARNING);
    }
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void MainWindow::RefreshDiagnostics()
{
    std::wstring report = sysInfo_.ToReport();
    report += L"\r\nConfig: " + paths::ConfigFilePath() + L"\r\n";
    report += L"Log: " + logging::LogFilePath() + L"\r\n";
    report += std::wstring(L"\r\nClipBack - developed by Haider: ") + kCreditUrl + L"\r\n";
    const EngineStatus s = engine_.Status();
    if (s.running) {
        report += str::Format(L"\r\nActive session: %s / %s (%s) %ux%u @ %u fps, buffered %.1f s, %s in RAM\r\n",
            s.captureName.c_str(), s.encoderName.c_str(), s.hardwareEncoder ? L"hardware" : L"software", s.width, s.height, s.fps,
            s.bufferedSeconds, str::BytesToHuman(s.bufferBytes).c_str());
    }
    SetWindowTextW(diagText_, report.c_str());
}

void MainWindow::OpenSaveFolder()
{
    const std::wstring dir = settings_.EffectiveSaveDirectory();
    paths::EnsureDirectory(dir);
    ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------------------
// Engine control
// ---------------------------------------------------------------------------

void MainWindow::UpdateCaptureExclusion()
{
    // While recording, keep this window out of the captured video (and out of screenshots/streams).
    // When idle it is a normal window again.
    const bool exclude = engine_.IsRunning() && settings_.excludeWindowFromCapture;
    SetWindowDisplayAffinity(hwnd_, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

bool MainWindow::StartReplay(bool notify)
{
    std::wstring error;
    if (!engine_.Start(settings_, error)) {
        LOG_ERROR(L"Start failed: %s", error.c_str());
        tray_.Notify(T(Str::MsgCouldNotStart), error, NIIF_ERROR);
        if (IsWindowVisible(hwnd_)) MessageBoxW(hwnd_, error.c_str(), T(Str::MsgCannotStartTitle), MB_ICONERROR | MB_OK);
        RefreshStatus();
        return false;
    }
    restartAttempts_ = 0;
    UpdateCaptureExclusion();
    if (matchWanted_ && !engine_.IsMatchRecording()) ScheduleMatchStart();   // continue the match after a restart
    if (notify) {
        const EngineStatus s = engine_.Status();
        tray_.Notify(T(Str::MsgRecordingTitle), str::Format(T(Str::MsgRecordingBody), settings_.hotkey.ToString().c_str(), settings_.replayDurationSec, s.encoderName.c_str()), NIIF_INFO);
    }
    RefreshStatus();
    return true;
}

void MainWindow::StopReplay()
{
    matchWanted_ = false;   // stopping everything also ends (and saves) the match recording
    matchRetryPending_ = false;
    KillTimer(hwnd_, kMatchRetryTimer);
    engine_.Stop();
    UpdateCaptureExclusion();
    RefreshStatus();
}

void MainWindow::SaveReplay()
{
    std::wstring error;
    if (!engine_.SaveReplay(error)) {
        tray_.Notify(T(Str::MsgNotSaved), error, NIIF_WARNING);
        LOG_WARN(L"Save request rejected: %s", error.c_str());
    }
    RefreshStatus();
}

void MainWindow::ToggleMatchRecording()
{
    if (engine_.IsMatchRecording() || matchRetryPending_) {
        matchWanted_ = false;
        matchRetryPending_ = false;
        KillTimer(hwnd_, kMatchRetryTimer);
        engine_.StopMatchRecording();   // finalizes the MP4 (MatchSaved event follows)
        RefreshStatus();
        return;
    }
    matchWanted_ = true;
    matchRetries_ = 0;
    if (!engine_.IsRunning()) {
        if (!StartReplay(false)) { matchWanted_ = false; return; }   // StartReplay schedules the match start
        return;
    }
    TryStartMatch();
}

void MainWindow::ScheduleMatchStart()
{
    matchRetryPending_ = true;
    SetTimer(hwnd_, kMatchRetryTimer, 1500, nullptr);   // give the encoder a moment to produce its first frames
    RefreshStatus();
}

void MainWindow::TryStartMatch()
{
    matchRetryPending_ = false;
    KillTimer(hwnd_, kMatchRetryTimer);
    if (!matchWanted_ || exiting_) return;
    std::wstring error;
    if (engine_.StartMatchRecording(error)) { RefreshStatus(); return; }
    if (engine_.IsRunning() && ++matchRetries_ <= 5 && error.find(L"Nothing recorded yet") != std::wstring::npos) {
        ScheduleMatchStart();
        return;
    }
    matchWanted_ = false;
    LOG_ERROR(L"Match recording could not start: %s", error.c_str());
    MessageBeep(MB_ICONHAND);
    tray_.Notify(T(Str::MsgMatchFailedTitle), error, NIIF_ERROR);
    RefreshStatus();
}

void MainWindow::OnEngineEvent(const EngineEvent& e)
{
    switch (e.type) {
    case EngineEventType::Started:
    case EngineEventType::Stopped:
        UpdateCaptureExclusion();
        RefreshStatus();
        break;
    case EngineEventType::SaveStarted:
        tray_.Notify(L"ClipBack", str::Format(T(Str::MsgSavingBody), e.seconds), NIIF_INFO);
        RefreshStatus();
        break;
    case EngineEventType::SaveCompleted: {
        const size_t slash = e.path.find_last_of(L"\\/");
        tray_.Notify(T(Str::MsgSavedTitle), str::Format(T(Str::MsgSavedBody), e.seconds, str::BytesToHuman(e.bytes).c_str()) + L"\n" + (slash == std::wstring::npos ? e.path : e.path.substr(slash + 1)), NIIF_INFO);
        RefreshStatus();
        break;
    }
    case EngineEventType::SaveFailed:
        tray_.Notify(T(Str::MsgSaveFailedTitle), e.message, NIIF_ERROR);
        RefreshStatus();
        break;
    case EngineEventType::MatchStarted:
        matchRetries_ = 0;
        MessageBeep(MB_ICONASTERISK);   // audible confirmation while the game is fullscreen
        tray_.Notify(T(Str::MsgMatchStartedTitle), str::Format(T(Str::MsgMatchStartedBody), MatchHotkeyText().c_str()), NIIF_INFO);
        RefreshStatus();
        break;
    case EngineEventType::MatchSaved: {
        MessageBeep(MB_OK);
        const size_t slash = e.path.find_last_of(L"\\/");
        tray_.Notify(T(Str::MsgMatchSavedTitle), FormatClock(e.seconds) + L"  (" + str::BytesToHuman(e.bytes) + L")\n" + (slash == std::wstring::npos ? e.path : e.path.substr(slash + 1)), NIIF_INFO);
        // Stopped by the engine itself (disk full / write error) while replay keeps running: do not restart it.
        if (engine_.IsRunning() && !engine_.IsMatchRecording() && !matchRetryPending_) matchWanted_ = false;
        RefreshStatus();
        break;
    }
    case EngineEventType::MatchFailed:
        MessageBeep(MB_ICONHAND);
        tray_.Notify(T(Str::MsgMatchFailedTitle), e.message, NIIF_ERROR);
        if (engine_.IsRunning() && !engine_.IsMatchRecording() && !matchRetryPending_) matchWanted_ = false;
        RefreshStatus();
        break;
    case EngineEventType::Error:
        tray_.Notify(T(e.fatal ? Str::MsgStoppedTitle : Str::MsgWarningTitle), e.message, e.fatal ? NIIF_ERROR : NIIF_WARNING);
        if (e.fatal) {
            engine_.Stop();
            UpdateCaptureExclusion();
            RefreshStatus();
            if (restartAttempts_ < 3) {
                ++restartAttempts_;
                LOG_INFO(L"Scheduling automatic restart (%d/3)", restartAttempts_);
                SetTimer(hwnd_, kRestartTimer, 3000, nullptr);
            }
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------

void MainWindow::ShowTrayMenu()
{
    HMENU menu = CreatePopupMenu();
    const bool running = engine_.IsRunning();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN, T(Str::TrayOpen));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_TOGGLE, T(running ? Str::TrayStop : Str::TrayStart));
    AppendMenuW(menu, MF_STRING | (running ? 0 : MF_GRAYED), IDM_TRAY_SAVE, (std::wstring(T(Str::TraySave)) + L"\t" + settings_.hotkey.ToString()).c_str());
    const bool matchOn = engine_.IsMatchRecording();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_MATCH, (std::wstring(T(matchOn ? Str::TrayMatchStop : Str::TrayMatchStart)) + L"\t" + MatchHotkeyText()).c_str());
    AppendMenuW(menu, MF_STRING, IDM_TRAY_OPEN_FOLDER, T(Str::TrayOpenFolder));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, T(Str::TraySettings));
    AppendMenuW(menu, MF_STRING, IDM_TRAY_ABOUT, T(Str::TrayAbout));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, T(Str::TrayExit));
    SetMenuDefaultItem(menu, IDM_TRAY_OPEN, FALSE);

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);   // required so the menu closes when clicking elsewhere
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | (IsRtl() ? TPM_LAYOUTRTL : 0), pt.x, pt.y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void MainWindow::OnTrayMessage(WPARAM, LPARAM lp)
{
    switch (LOWORD(lp)) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        Show();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowTrayMenu();
        break;
    case NIN_BALLOONUSERCLICK: {
        const EngineStatus s = engine_.Status();
        if (!s.lastSavedFile.empty()) {
            ShellExecuteW(hwnd_, L"open", L"explorer.exe", (L"/select,\"" + s.lastSavedFile + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    }
    default: break;
    }
}

void MainWindow::OpenCreditLink()
{
    ShellExecuteW(hwnd_, L"open", kCreditUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

// Destroys and recreates every child control (used when the UI language / text direction changes).
void MainWindow::RebuildControls()
{
    const Page current = page_;
    std::vector<HWND> children;
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) children.push_back(child);
    for (HWND child : children) DestroyWindow(child);
    replayControls_.clear();
    settingsControls_.clear();
    diagControls_.clear();
    settingsLabels_.clear();
    infoRows_.clear();
    tabs_ = creditLink_ = nullptr;

    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    ex = IsRtl() ? (ex | WS_EX_LAYOUTRTL | WS_EX_RTLREADING) : (ex & ~(WS_EX_LAYOUTRTL | WS_EX_RTLREADING));
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    CreateControls();
    Layout();
    TabCtrl_SetCurSel(tabs_, static_cast<int>(current));
    SelectPage(current);
    RefreshStatus();
}

void MainWindow::Exit()
{
    exiting_ = true;
    KillTimer(hwnd_, kStatusTimer);
    KillTimer(hwnd_, kRestartTimer);
    KillTimer(hwnd_, kMatchRetryTimer);
    hotkeys_.Unregister();
    if (matchHotkeyOk_) UnregisterHotKey(hwnd_, kMatchHotkeyId);
    engine_.SetEventCallback(nullptr);
    engine_.Stop();
    tray_.Remove();
    DestroyWindow(hwnd_);
}

// ---------------------------------------------------------------------------
// Commands / message loop
// ---------------------------------------------------------------------------

void MainWindow::OnCommand(int id, int code, HWND)
{
    switch (id) {
    case IDM_TRAY_OPEN: Show(); break;
    case IDM_TRAY_TOGGLE:
    case IDC_BTN_TOGGLE:
        if (engine_.IsRunning()) StopReplay(); else StartReplay(true);
        break;
    case IDM_TRAY_SAVE:
    case IDC_BTN_SAVE: SaveReplay(); break;
    case IDM_TRAY_MATCH:
    case IDC_BTN_MATCH: ToggleMatchRecording(); break;
    case IDM_TRAY_OPEN_FOLDER:
    case IDC_BTN_OPEN_FOLDER: OpenSaveFolder(); break;
    case IDM_TRAY_SETTINGS: ShowSettings(); break;
    case IDM_TRAY_ABOUT:
    case IDC_CREDIT_LINK: OpenCreditLink(); break;
    case IDM_TRAY_EXIT: Exit(); break;
    case IDC_BTN_APPLY: ApplySettings(); break;
    case IDC_BTN_DEFAULTS: {
        Settings defaults;
        defaults.language = settings_.language;
        defaults.startWithWindows = startup::IsEnabled();
        settings_ = defaults;
        LoadSettingsIntoControls();
        break;
    }
    case IDC_BTN_BROWSE: BrowseForFolder(); break;
    case IDC_CMB_BITRATE: if (code == CBN_SELCHANGE) UpdateBitrateEditState(); break;
    case IDC_CHK_MIC: EnableWindow(cmbMic_, Button_GetCheck(chkMic_) == BST_CHECKED); break;
    case IDC_BTN_DIAG_REFRESH:
        sysInfo_ = CollectSystemInfo();
        RefreshDiagnostics();
        micDevices_ = sysInfo_.captureDevices;
        outputDevices_ = sysInfo_.renderDevices;
        FillDeviceCombo(cmbMic_, micDevices_, settings_.microphoneDeviceId);
        FillDeviceCombo(cmbOutput_, outputDevices_, settings_.outputDeviceId);
        break;
    case IDC_BTN_DIAG_COPY: {
        const int len = GetWindowTextLengthW(diagText_);
        std::wstring text(static_cast<size_t>(len) + 1, L'\0');
        GetWindowTextW(diagText_, text.data(), len + 1);
        if (OpenClipboard(hwnd_)) {
            EmptyClipboard();
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size()) * sizeof(wchar_t));
            if (mem) {
                memcpy(GlobalLock(mem), text.c_str(), text.size() * sizeof(wchar_t));
                GlobalUnlock(mem);
                SetClipboardData(CF_UNICODETEXT, mem);
            }
            CloseClipboard();
        }
        break;
    }
    case IDC_BTN_DIAG_LOG:
        ShellExecuteW(hwnd_, L"open", logging::LogFilePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    default: break;
    }
}

LRESULT MainWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        dpi_ = GetDpiForWindow(hwnd_);
        CreateFonts();
        CreateControls();
        Layout();
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        CreateFonts();
        auto apply = [&](const std::vector<HWND>& list, HFONT f) { for (HWND h : list) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(f), TRUE); };
        apply(replayControls_, fontNormal_);
        apply(settingsControls_, fontNormal_);
        apply(diagControls_, fontNormal_);
        for (auto& r : infoRows_) SendMessageW(r.value, WM_SETFONT, reinterpret_cast<WPARAM>(fontBold_), TRUE);
        SendMessageW(statusHeader_, WM_SETFONT, reinterpret_cast<WPARAM>(fontHeader_), TRUE);
        SendMessageW(diagText_, WM_SETFONT, reinterpret_cast<WPARAM>(fontMono_), TRUE);
        SendMessageW(tabs_, WM_SETFONT, reinterpret_cast<WPARAM>(fontNormal_), TRUE);
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
        Layout();
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        PaintHeader(dc);
        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(hwnd_, &r);
        FillRect(reinterpret_cast<HDC>(wp), &r, pageBrush_);
        return 1;
    }

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        HWND ctl = reinterpret_cast<HWND>(lp);
        SetBkColor(dc, kPageBg);
        if (ctl == creditLink_) {
            SetBkColor(dc, kAccent);
            SetTextColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(accentBrush_);
        }
        if (ctl == statusHeader_) SetTextColor(dc, engine_.IsRunning() ? kGreen : kTextMuted);
        else if (ctl == statusSub_) SetTextColor(dc, kTextMuted);
        else if (ctl == diagText_) SetTextColor(dc, RGB(32, 32, 32));
        else {
            bool isLabel = false;
            for (auto& r : infoRows_) if (r.label == ctl) isLabel = true;
            for (HWND h : settingsLabels_) if (h == ctl) isLabel = true;
            SetTextColor(dc, isLabel ? kTextMuted : RGB(16, 16, 16));
        }
        return reinterpret_cast<LRESULT>(pageBrush_);
    }

    case WM_CTLCOLORBTN:
        return reinterpret_cast<LRESULT>(pageBrush_);

    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        if (creditLink_ && WindowFromPoint(pt) == creditLink_) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->hwndFrom == tabs_ && hdr->code == TCN_SELCHANGE) {
            SelectPage(static_cast<Page>(TabCtrl_GetCurSel(tabs_)));
        }
        return 0;
    }

    case WM_COMMAND:
        OnCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
        return 0;

    case WM_HOTKEY:
        if (static_cast<int>(wp) == HotkeyManager::kSaveReplayId) SaveReplay();
        else if (static_cast<int>(wp) == kMatchHotkeyId) ToggleMatchRecording();
        return 0;

    case WM_TIMER:
        if (wp == kStatusTimer) {
            if (IsWindowVisible(hwnd_) && page_ == Page::Replay) RefreshStatus();
        } else if (wp == kMatchRetryTimer) {
            TryStartMatch();
        } else if (wp == kRestartTimer) {
            KillTimer(hwnd_, kRestartTimer);
            if (!engine_.IsRunning() && !exiting_) {
                LOG_INFO(L"Automatic restart attempt %d", restartAttempts_);
                const int attempts = restartAttempts_;
                if (!StartReplay(false)) {
                    restartAttempts_ = attempts;   // StartReplay resets on success only
                }
            }
        }
        return 0;

    case TrayIcon::kCallbackMessage:
        OnTrayMessage(wp, lp);
        return 0;

    case MainWindow::kEngineEventMessage: {
        std::unique_ptr<EngineEvent> e(reinterpret_cast<EngineEvent*>(lp));
        if (e && !exiting_) OnEngineEvent(*e);
        return 0;
    }

    case WM_CLOSE:
        // Closing the window keeps recording in the tray - Exit is in the tray menu.
        ShowWindow(hwnd_, SW_HIDE);
        if (tray_.IsAdded()) {
            static bool hintShown = false;
            if (!hintShown) {
                hintShown = true;
                tray_.Notify(T(Str::MsgStillRunningTitle), T(Str::MsgStillRunningBody), NIIF_INFO);
            }
        } else {
            Exit();
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wp) Exit();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace clipback
