cmake_minimum_required(VERSION 3.21)

project(ClipBack VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "ClipBack targets Windows and must be built with the MSVC toolchain (Visual Studio 2022 or newer).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT so the installer does not need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CLIPBACK_SOURCES
    src/main.cpp
    src/common/StringUtils.cpp
    src/logging/Logger.cpp
    src/settings/Json.cpp
    src/settings/Settings.cpp
    src/system/Paths.cpp
    src/system/SystemInfo.cpp
    src/system/Startup.cpp
    src/capture/D3DDevice.cpp
    src/capture/WgcCapture.cpp
    src/capture/DuplicationCapture.cpp
    src/encoder/VideoConverter.cpp
    src/encoder/VideoEncoder.cpp
    src/encoder/H264Utils.cpp
    src/buffer/RingBuffer.cpp
    src/audio/WasapiCapture.cpp
    src/audio/AacEncoder.cpp
    src/audio/AudioMixer.cpp
    src/muxer/Mp4Writer.cpp
    src/hotkeys/HotkeyManager.cpp
    src/engine/ReplayEngine.cpp
    src/ui/TrayIcon.cpp
    src/ui/MainWindow.cpp
    src/ui/Strings.cpp
    resources/ClipBack.rc
)

set(CLIPBACK_HEADERS
    src/common/Common.h
    src/common/StringUtils.h
    src/logging/Logger.h
    src/settings/Json.h
    src/settings/Settings.h
    src/system/Paths.h
    src/system/SystemInfo.h
    src/system/Startup.h
    src/capture/D3DDevice.h
    src/capture/ICaptureSource.h
    src/capture/WgcCapture.h
    src/capture/DuplicationCapture.h
    src/encoder/VideoConverter.h
    src/encoder/VideoEncoder.h
    src/encoder/H264Utils.h
    src/buffer/RingBuffer.h
    src/audio/WasapiCapture.h
    src/audio/AacEncoder.h
    src/audio/AudioMixer.h
    src/muxer/Mp4Writer.h
    src/hotkeys/HotkeyManager.h
    src/engine/ReplayEngine.h
    src/ui/TrayIcon.h
    src/ui/MainWindow.h
    src/ui/Strings.h
    src/ui/resource.h
)

add_executable(ClipBack WIN32 ${CLIPBACK_SOURCES} ${CLIPBACK_HEADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${CLIPBACK_SOURCES} ${CLIPBACK_HEADERS})

target_include_directories(ClipBack PRIVATE src)

target_compile_definitions(ClipBack PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000006   # Windows 10 1903 headers (WGC)
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(ClipBack PRIVATE
    /W4 /permissive- /EHsc /utf-8 /bigobj /Zc:__cplusplus /MP
    /wd4100   # unreferenced formal parameter (COM interface stubs)
    $<$<CONFIG:Release>:/O2 /GL /Gy>
)

target_link_options(ClipBack PRIVATE
    /MANIFEST:NO                      # the manifest is embedded through the .rc file
    /DYNAMICBASE /NXCOMPAT
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
)

target_link_libraries(ClipBack PRIVATE
    windowsapp        # C++/WinRT (Windows.Graphics.Capture)
    d3d11 dxgi
    mfplat mf mfreadwrite mfuuid
    ole32 oleaut32 uuid
    user32 gdi32 shell32 shlwapi comctl32 dwmapi uxtheme msimg32
    avrt propsys shcore advapi32
    strmiids          # CODECAPI_* GUIDs
)

# Windows Graphics Capture interop headers live in the Windows SDK's cppwinrt folder; Visual Studio
# adds it automatically. Fail early with a helpful message if the SDK is too old.
if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION VERSION_LESS "10.0.19041.0")
        message(WARNING "Windows SDK ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION} is older than 10.0.19041 - install a newer SDK via the Visual Studio Installer.")
    endif()
endif()

set_target_properties(ClipBack PROPERTIES
    OUTPUT_NAME "ClipBack"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:ClipBack>"
)

install(TARGETS ClipBack RUNTIME DESTINATION .)
install(FILES README.md LICENSE.txt DESTINATION .)
