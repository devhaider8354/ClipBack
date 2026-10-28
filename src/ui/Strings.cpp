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
};

static_assert(sizeof(kEnglish) / sizeof(kEnglish[0]) == static_cast<size_t>(Str::Count), "English string table out of sync with Str enum");
static_assert(sizeof(kArabic) / sizeof(kArabic[0]) == static_cast<size_t>(Str::Count), "Arabic string table out of sync with Str enum");

} // namespace

void SetLanguage(Language lang) { g_language = lang; }
Language CurrentLanguage() { return g_language; }
bool IsRtl() { return g_language == Language::Arabic; }

const wchar_t* T(Str id)
{
    const size_t i = static_cast<size_t>(id);
    if (i >= static_cast<size_t>(Str::Count)) return L"";
    return g_language == Language::Arabic ? kArabic[i] : kEnglish[i];
}

Language LanguageFromString(const std::string& s)
{
    if (s == "ar" || s == "arabic") return Language::Arabic;
    if (s == "en" || s == "english") return Language::English;
    return DetectSystemLanguage();
}

std::string LanguageToString(Language lang)
{
    return lang == Language::Arabic ? "ar" : "en";
}

Language DetectSystemLanguage()
{
    const LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_ARABIC ? Language::Arabic : Language::English;
}

} // namespace clipback
