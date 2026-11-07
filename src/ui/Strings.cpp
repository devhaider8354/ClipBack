#include "ui/Strings.h"

namespace clipback {

namespace {

Language g_language = Language::English;

// Index = static_cast<size_t>(Str)
const wchar_t* const kEnglish[] = {
    L"ClipBack",
    L"Instant replay for your games",
    L"by Haider",
    L"  Replay  ", L"  Settings  ", L"  Diagnostics  ",
    L"Status", L"Replay duration", L"Resolution", L"Frame rate", L"Bitrate", L"Encoder", L"Capture method", L"Audio", L"Hotkey", L"Save location", L"Buffered", L"Buffer memory", L"Last saved",
    L"\x25CF  Recording", L"\x25CB  Stopped",
    L"Keeping the last %d seconds in memory. Press %s to save.", L"Press Start Replay to begin buffering gameplay.", L"Saving replay...",
    L"Recording (saving...)", L"Recording", L"Stopped", L"%d seconds", L"Same as game", L"%u FPS", L"%.1f Mbps", L"  (hardware)", L"  (software)",
    L"Game audio + microphone", L"Game audio", L"Unavailable", L"  (not registered!)", L"%.0f s  (%llu frames, %llu dropped)",
    L"Start Replay", L"Stop Replay", L"Save Replay", L"Open Folder",
    L"Language", L"Replay duration", L"Resolution", L"Frame rate", L"Bitrate", L"Encoder", L"Display to capture", L"Save replay hotkey", L"Save folder", L"Game audio device", L"Microphone",
    L"30 seconds", L"60 seconds (default)", L"120 seconds", L"Same as game / display", L"1920 x 1080", L"1280 x 720", L"60 FPS", L"30 FPS", L"8 Mbps", L"12 Mbps", L"16 Mbps", L"20 Mbps", L"Custom (kbps)",
    L"Auto (best hardware encoder)", L"NVIDIA NVENC", L"AMD AMF", L"Intel Quick Sync", L"CPU (software fallback)", L"Display %u  (%dx%d @ %d Hz)", L"  - primary", L"Primary display", L"Default device",
    L"Record microphone", L"Capture mouse cursor", L"Start ClipBack with Windows", L"Start minimized to tray", L"Start replay automatically on launch", L"Browse...", L"Save settings", L"Reset defaults",
    L"English", L"العربية",
    L"Refresh", L"Copy to clipboard", L"Open log file",
    L"Custom bitrate must be between 1000 and 200000 kbps", L"Please choose a hotkey for saving replays", L"Cannot create the save folder: ", L"Settings could not be written:\n", L"Settings saved.", L"Choose the replay save folder",
    L"Hotkey problem", L"ClipBack could not start", L"ClipBack - cannot start", L"ClipBack is recording", L"Press %s to save the last %d seconds. Encoder: %s", L"Replay not saved", L"Replay saved", L"Replay save failed",
    L"ClipBack stopped", L"ClipBack warning", L"ClipBack is still running", L"Recording continues in the background. Right-click the tray icon to exit.", L"Saving the last %.0f seconds...", L"Saved %.0f s replay (%s)", L"Nothing recorded yet - wait a second and try again",
    L"Open ClipBack", L"Start Replay", L"Stop Replay", L"Save Replay", L"Open replay folder", L"Settings", L"Exit", L"About - by Haider", L"ClipBack - recording", L"ClipBack - stopped",
    L"Match recording", L"Off  -  press %s to record the whole match", L"\x25CF  %s   (%s)", L"Record Match  (%s)", L"Stop Match Recording", L"Record whole match", L"Stop match recording",
    L"Match recording started", L"Recording everything to disk until you press %s again.", L"Match recording saved", L"Match recording failed",
    L"Hide yellow capture border",
    L"Português (Brasil)",
};

const wchar_t* const kArabic[] = {
    L"ClipBack",
    L"إعادة فورية لأفضل لحظات ألعابك",
    L"بواسطة حيدر",
    L"  الإعادة  ", L"  الإعدادات  ", L"  التشخيص  ",
    L"الحالة", L"مدة الإعادة", L"الدقة", L"معدل الإطارات", L"معدل البت", L"المشفّر", L"طريقة الالتقاط", L"الصوت", L"مفتاح الاختصار", L"مكان الحفظ", L"المخزّن مؤقتاً", L"ذاكرة المخزن", L"آخر حفظ",
    L"\x25CF  جارٍ التسجيل", L"\x25CB  متوقف",
    L"يتم الاحتفاظ بآخر %d ثانية في الذاكرة. اضغط %s للحفظ.", L"اضغط بدء الإعادة لبدء تخزين اللعب مؤقتاً.", L"جارٍ حفظ الإعادة...",
    L"جارٍ التسجيل (يتم الحفظ...)", L"جارٍ التسجيل", L"متوقف", L"%d ثانية", L"مثل اللعبة", L"%u إطار/ث", L"%.1f ميغابت/ث", L"  (عتاد)", L"  (برمجي)",
    L"صوت اللعبة + المايكروفون", L"صوت اللعبة", L"غير متاح", L"  (غير مسجّل!)", L"%.0f ث  (%llu إطار، %llu متجاهَل)",
    L"بدء الإعادة", L"إيقاف الإعادة", L"حفظ الإعادة", L"فتح المجلد",
    L"اللغة", L"مدة الإعادة", L"الدقة", L"معدل الإطارات", L"معدل البت", L"المشفّر", L"الشاشة المراد التقاطها", L"مفتاح حفظ الإعادة", L"مجلد الحفظ", L"جهاز صوت اللعبة", L"المايكروفون",
    L"30 ثانية", L"60 ثانية (افتراضي)", L"120 ثانية", L"مثل اللعبة / الشاشة", L"1920 x 1080", L"1280 x 720", L"60 إطار/ث", L"30 إطار/ث", L"8 ميغابت/ث", L"12 ميغابت/ث", L"16 ميغابت/ث", L"20 ميغابت/ث", L"مخصص (كيلوبت/ث)",
    L"تلقائي (أفضل مشفّر عتادي)", L"NVIDIA NVENC", L"AMD AMF", L"Intel Quick Sync", L"المعالج (بديل برمجي)", L"الشاشة %u  (%dx%d @ %d Hz)", L"  - الرئيسية", L"الشاشة الرئيسية", L"الجهاز الافتراضي",
    L"تسجيل المايكروفون", L"التقاط مؤشر الماوس", L"تشغيل ClipBack مع Windows", L"البدء مصغّراً في شريط المهام", L"بدء الإعادة تلقائياً عند التشغيل", L"استعراض...", L"حفظ الإعدادات", L"استعادة الافتراضي",
    L"English", L"العربية",
    L"تحديث", L"نسخ إلى الحافظة", L"فتح ملف السجل",
    L"معدل البت المخصص يجب أن يكون بين 1000 و 200000 كيلوبت/ث", L"يرجى اختيار مفتاح اختصار لحفظ الإعادة", L"تعذّر إنشاء مجلد الحفظ: ", L"تعذّر كتابة الإعدادات:\n", L"تم حفظ الإعدادات.", L"اختر مجلد حفظ الإعادات",
    L"مشكلة في مفتاح الاختصار", L"تعذّر تشغيل ClipBack", L"ClipBack - تعذّر البدء", L"ClipBack يسجّل الآن", L"اضغط %s لحفظ آخر %d ثانية. المشفّر: %s", L"لم يتم حفظ الإعادة", L"تم حفظ الإعادة", L"فشل حفظ الإعادة",
    L"توقف ClipBack", L"تنبيه من ClipBack", L"ClipBack ما زال يعمل", L"يستمر التسجيل في الخلفية. انقر بالزر الأيمن على أيقونة شريط المهام للخروج.", L"جارٍ حفظ آخر %.0f ثانية...", L"تم حفظ إعادة بطول %.0f ث (%s)", L"لم يُسجَّل شيء بعد - انتظر لحظة ثم حاول مجدداً",
    L"فتح ClipBack", L"بدء الإعادة", L"إيقاف الإعادة", L"حفظ الإعادة", L"فتح مجلد الإعادات", L"الإعدادات", L"خروج", L"حول البرنامج - بواسطة حيدر", L"ClipBack - يسجّل", L"ClipBack - متوقف",
    L"تسجيل المباراة", L"متوقف  -  اضغط %s لتسجيل المباراة كاملة", L"\x25CF  %s   (%s)", L"تسجيل كامل  (%s)", L"إيقاف التسجيل الكامل", L"تسجيل المباراة كاملة", L"إيقاف تسجيل المباراة",
    L"بدأ تسجيل المباراة", L"يتم تسجيل كل شيء على القرص حتى تضغط %s مرة أخرى.", L"تم حفظ تسجيل المباراة", L"فشل تسجيل المباراة",
    L"\u0625\u062e\u0641\u0627\u0621 \u062d\u062f \u0627\u0644\u0627\u0644\u062a\u0642\u0627\u0637 \u0627\u0644\u0623\u0635\u0641\u0631",
    L"Português (Brasil)",
};

static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == static_cast<size_t>(Str::Count), "English string table out of sync with Str enum");
static_assert(sizeof(kArabic) / sizeof(kArabic[0]) == static_cast<size_t>(Str::Count), "Arabic string table out of sync with Str enum");

const wchar_t* Portuguese(Str id)
{
    switch (id) {
    case Str::AppTitle: return L"ClipBack";
    case Str::Tagline: return L"Replay instantâneo para seus jogos";
    case Str::CreditLink: return L"por Haider";
    case Str::TabReplay: return L"  Replay  "; case Str::TabSettings: return L"  Configurações  "; case Str::TabDiagnostics: return L"  Diagnóstico  ";
    case Str::LblStatus: return L"Status"; case Str::LblDuration: return L"Duração do replay"; case Str::LblResolution: return L"Resolução"; case Str::LblFrameRate: return L"Taxa de quadros"; case Str::LblBitrate: return L"Taxa de bits"; case Str::LblEncoder: return L"Codificador"; case Str::LblCapture: return L"Método de captura"; case Str::LblAudio: return L"Áudio"; case Str::LblHotkey: return L"Atalho"; case Str::LblSaveLocation: return L"Local de salvamento"; case Str::LblBuffered: return L"Em buffer"; case Str::LblBufferMemory: return L"Memória do buffer"; case Str::LblLastSaved: return L"Último salvo";
    case Str::StatusRecording: return L"\x25CF  Gravando"; case Str::StatusStopped: return L"\x25CB  Parado";
    case Str::SubRecording: return L"Mantendo os últimos %d segundos na memória. Pressione %s para salvar."; case Str::SubStopped: return L"Pressione Iniciar Replay para começar a gravar em buffer."; case Str::SubSaving: return L"Salvando replay...";
    case Str::ValRecordingSaving: return L"Gravando (salvando...)"; case Str::ValRecording: return L"Gravando"; case Str::ValStopped: return L"Parado"; case Str::ValSeconds: return L"%d segundos"; case Str::ValSameAsGame: return L"Igual ao jogo"; case Str::ValFps: return L"%u FPS"; case Str::ValMbps: return L"%.1f Mbps"; case Str::ValHardware: return L"  (hardware)"; case Str::ValSoftware: return L"  (software)";
    case Str::ValGameAudioMic: return L"Áudio do jogo + microfone"; case Str::ValGameAudio: return L"Áudio do jogo"; case Str::ValUnavailable: return L"Indisponível"; case Str::ValHotkeyNotRegistered: return L"  (não registrado!)"; case Str::ValBuffered: return L"%.0f s  (%llu quadros, %llu descartados)";
    case Str::BtnStartReplay: return L"Iniciar Replay"; case Str::BtnStopReplay: return L"Parar Replay"; case Str::BtnSaveReplay: return L"Salvar Replay"; case Str::BtnOpenFolder: return L"Abrir Pasta";
    case Str::SetLanguage: return L"Idioma"; case Str::SetDuration: return L"Duração do replay"; case Str::SetResolution: return L"Resolução"; case Str::SetFrameRate: return L"Taxa de quadros"; case Str::SetBitrate: return L"Taxa de bits"; case Str::SetEncoder: return L"Codificador"; case Str::SetDisplay: return L"Monitor para capturar"; case Str::SetHotkey: return L"Atalho para salvar replay"; case Str::SetSaveFolder: return L"Pasta para salvar"; case Str::SetOutputDevice: return L"Dispositivo de áudio"; case Str::SetMicrophone: return L"Microfone";
    case Str::Dur30: return L"30 segundos"; case Str::Dur60: return L"60 segundos (padrão)"; case Str::Dur120: return L"120 segundos"; case Str::ResSame: return L"Igual ao jogo / monitor"; case Str::Res1080: return L"1920 x 1080"; case Str::Res720: return L"1280 x 720"; case Str::Fps60: return L"60 FPS"; case Str::Fps30: return L"30 FPS"; case Str::Br8: return L"8 Mbps"; case Str::Br12: return L"12 Mbps"; case Str::Br16: return L"16 Mbps"; case Str::Br20: return L"20 Mbps"; case Str::BrCustom: return L"Personalizado (kbps)";
    case Str::EncAuto: return L"Automático (melhor codificador de hardware)"; case Str::EncNvenc: return L"NVIDIA NVENC"; case Str::EncAmf: return L"AMD AMF"; case Str::EncQsv: return L"Intel Quick Sync"; case Str::EncCpu: return L"CPU (alternativa por software)"; case Str::DisplayItem: return L"Monitor %u  (%dx%d @ %d Hz)"; case Str::DisplayPrimary: return L"  - principal"; case Str::DisplayPrimaryOnly: return L"Monitor principal"; case Str::DefaultDevice: return L"Dispositivo padrão";
    case Str::ChkRecordMic: return L"Gravar microfone"; case Str::ChkCursor: return L"Capturar cursor do mouse"; case Str::ChkStartup: return L"Iniciar ClipBack com o Windows"; case Str::ChkMinimized: return L"Iniciar minimizado na bandeja"; case Str::ChkAutoStart: return L"Iniciar replay automaticamente ao abrir"; case Str::BtnBrowse: return L"Procurar..."; case Str::BtnApply: return L"Salvar configurações"; case Str::BtnDefaults: return L"Restaurar padrões";
    case Str::LangEnglish: return L"English"; case Str::LangArabic: return L"العربية"; case Str::LangPortugueseBrazil: return L"Português (Brasil)";
    case Str::BtnRefresh: return L"Atualizar"; case Str::BtnCopy: return L"Copiar para a área de transferência"; case Str::BtnOpenLog: return L"Abrir arquivo de log";
    case Str::MsgBitrateRange: return L"A taxa de bits personalizada deve estar entre 1000 e 200000 kbps"; case Str::MsgChooseHotkey: return L"Escolha um atalho para salvar os replays"; case Str::MsgCannotCreateFolder: return L"Não foi possível criar a pasta de salvamento: "; case Str::MsgSettingsWriteFailed: return L"Não foi possível salvar as configurações:\n"; case Str::MsgSettingsSaved: return L"Configurações salvas."; case Str::DlgChooseFolder: return L"Escolha a pasta para salvar os replays";
    case Str::MsgHotkeyProblem: return L"Problema com o atalho"; case Str::MsgCouldNotStart: return L"Não foi possível iniciar o ClipBack"; case Str::MsgCannotStartTitle: return L"ClipBack - não foi possível iniciar"; case Str::MsgRecordingTitle: return L"ClipBack está gravando"; case Str::MsgRecordingBody: return L"Pressione %s para salvar os últimos %d segundos. Codificador: %s"; case Str::MsgNotSaved: return L"Replay não salvo"; case Str::MsgSavedTitle: return L"Replay salvo"; case Str::MsgSaveFailedTitle: return L"Falha ao salvar replay";
    case Str::MsgStoppedTitle: return L"ClipBack parado"; case Str::MsgWarningTitle: return L"Aviso do ClipBack"; case Str::MsgStillRunningTitle: return L"ClipBack ainda está em execução"; case Str::MsgStillRunningBody: return L"A gravação continua em segundo plano. Clique com o botão direito no ícone da bandeja para sair."; case Str::MsgSavingBody: return L"Salvando os últimos %.0f segundos..."; case Str::MsgSavedBody: return L"Replay de %.0f s salvo (%s)"; case Str::MsgWaitForBuffer: return L"Nada gravado ainda - espere um instante e tente novamente";
    case Str::TrayOpen: return L"Abrir ClipBack"; case Str::TrayStart: return L"Iniciar Replay"; case Str::TrayStop: return L"Parar Replay"; case Str::TraySave: return L"Salvar Replay"; case Str::TrayOpenFolder: return L"Abrir pasta de replays"; case Str::TraySettings: return L"Configurações"; case Str::TrayExit: return L"Sair"; case Str::TrayAbout: return L"Sobre - por Haider"; case Str::TipRecording: return L"ClipBack - gravando"; case Str::TipStopped: return L"ClipBack - parado";
    case Str::LblMatch: return L"Gravação da partida"; case Str::ValMatchOff: return L"Desativada  -  pressione %s para gravar a partida inteira"; case Str::ValMatchOn: return L"\x25CF  %s   (%s)"; case Str::BtnStartMatch: return L"Gravar Partida  (%s)"; case Str::BtnStopMatch: return L"Parar Gravação da Partida"; case Str::TrayMatchStart: return L"Gravar partida inteira"; case Str::TrayMatchStop: return L"Parar gravação da partida";
    case Str::MsgMatchStartedTitle: return L"Gravação da partida iniciada"; case Str::MsgMatchStartedBody: return L"Tudo será gravado no disco até você pressionar %s novamente."; case Str::MsgMatchSavedTitle: return L"Gravação da partida salva"; case Str::MsgMatchFailedTitle: return L"Falha na gravação da partida";
    case Str::ChkHideCaptureBorder: return L"Ocultar borda amarela da captura";
    default: return kEnglish[static_cast<size_t>(id)];
    }
}

} // namespace

void SetLanguage(Language lang) { g_language = lang; }
Language CurrentLanguage() { return g_language; }
bool IsRtl() { return g_language == Language::Arabic; }

const wchar_t* T(Str id)
{
    const size_t i = static_cast<size_t>(id);
    if (i >= static_cast<size_t>(Str::Count)) return L"";
    if (g_language == Language::Arabic) return kArabic[i];
    if (g_language == Language::PortugueseBrazil) return Portuguese(id);
    return kEnglish[i];
}

Language LanguageFromString(const std::string& s)
{
    if (s == "ar" || s == "arabic") return Language::Arabic;
    if (s == "pt" || s == "pt-br" || s == "portuguese" || s == "brazilian") return Language::PortugueseBrazil;
    if (s == "en" || s == "english") return Language::English;
    return DetectSystemLanguage();
}

std::string LanguageToString(Language lang)
{
    if (lang == Language::Arabic) return "ar";
    if (lang == Language::PortugueseBrazil) return "pt-BR";
    return "en";
}

Language DetectSystemLanguage()
{
    const LANGID id = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(id) == LANG_ARABIC) return Language::Arabic;
    if (PRIMARYLANGID(id) == LANG_PORTUGUESE) return Language::PortugueseBrazil;
    return Language::English;
}

} // namespace clipback
