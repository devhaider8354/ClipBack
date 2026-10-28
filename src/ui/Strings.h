// UI localisation: English / Arabic string tables.
#pragma once
#include "common/Common.h"
#include <string>

namespace clipback {

enum class Language { English = 0, Arabic = 1 };

enum class Str {
    AppTitle, Tagline, CreditLink,
    TabReplay, TabSettings, TabDiagnostics,
    // Replay page labels
    LblStatus, LblDuration, LblResolution, LblFrameRate, LblBitrate, LblEncoder, LblCapture, LblAudio, LblHotkey, LblSaveLocation, LblBuffered, LblBufferMemory, LblLastSaved,
    StatusRecording, StatusStopped, SubRecording, SubStopped, SubSaving,
    ValRecordingSaving, ValRecording, ValStopped, ValSeconds, ValSameAsGame, ValFps, ValMbps, ValHardware, ValSoftware,
    ValGameAudioMic, ValGameAudio, ValUnavailable, ValHotkeyNotRegistered, ValBuffered,
    BtnStartReplay, BtnStopReplay, BtnSaveReplay, BtnOpenFolder,
    // Settings page
    SetLanguage, SetDuration, SetResolution, SetFrameRate, SetBitrate, SetEncoder, SetDisplay, SetHotkey, SetSaveFolder, SetOutputDevice, SetMicrophone,
    Dur30, Dur60, Dur120, ResSame, Res1080, Res720, Fps60, Fps30, Br8, Br12, Br16, Br20, BrCustom,
    EncAuto, EncNvenc, EncAmf, EncQsv, EncCpu, DisplayItem, DisplayPrimary, DisplayPrimaryOnly, DefaultDevice,
    ChkRecordMic, ChkCursor, ChkStartup, ChkMinimized, ChkAutoStart, BtnBrowse, BtnApply, BtnDefaults,
    LangEnglish, LangArabic,
    // Diagnostics
    BtnRefresh, BtnCopy, BtnOpenLog,
    // Messages
    MsgBitrateRange, MsgChooseHotkey, MsgCannotCreateFolder, MsgSettingsWriteFailed, MsgSettingsSaved, DlgChooseFolder,
    MsgHotkeyProblem, MsgCouldNotStart, MsgCannotStartTitle, MsgRecordingTitle, MsgRecordingBody, MsgNotSaved, MsgSavedTitle, MsgSaveFailedTitle,
    MsgStoppedTitle, MsgWarningTitle, MsgStillRunningTitle, MsgStillRunningBody, MsgSavingBody, MsgSavedBody, MsgWaitForBuffer,
    // Tray
    TrayOpen, TrayStart, TrayStop, TraySave, TrayOpenFolder, TraySettings, TrayExit, TrayAbout, TipRecording, TipStopped,
    // Match (continuous) recording
    LblMatch, ValMatchOff, ValMatchOn, BtnStartMatch, BtnStopMatch, TrayMatchStart, TrayMatchStop,
    MsgMatchStartedTitle, MsgMatchStartedBody, MsgMatchSavedTitle, MsgMatchFailedTitle,
    Count
};

void SetLanguage(Language lang);
Language CurrentLanguage();
bool IsRtl();
const wchar_t* T(Str id);
Language LanguageFromString(const std::string& s);   // "ar" / "en" / "" (= system)
std::string LanguageToString(Language lang);
Language DetectSystemLanguage();

constexpr wchar_t kCreditUrl[] = L"https://bio.byhdr.com/king";

} // namespace clipback
