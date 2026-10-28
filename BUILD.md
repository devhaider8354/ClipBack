# Building ClipBack

## Requirements

| Component | Notes |
|-----------|-------|
| Windows 10 (build 17763) or Windows 11 | Windows 10 **1903+** is required at runtime for Windows Graphics Capture; older builds fall back to Desktop Duplication. |
| Visual Studio 2022 (17.x) or 2026 | Workload **"Desktop development with C++"**. Make sure these individual components are ticked: *MSVC v143 (or newer) x64/x86 build tools*, *Windows 11 SDK (10.0.22621 or newer)* (10.0.19041 minimum), *C++ CMake tools for Windows*. |
| CMake 3.21+ | Bundled with the VS CMake component, or install from cmake.org. |
| Inno Setup 6 (optional) | Only for building the installer: https://jrsoftware.org/isinfo.php |

No third-party libraries are used. C++/WinRT headers (`winrt/*.h`) ship inside the Windows SDK
(`Include\<ver>\cppwinrt`) and Visual Studio adds that folder to the include path automatically.

## Build (command line)

Open **"x64 Native Tools Command Prompt for VS 2022"** (Start menu), then:

```bat
cd C:\Users\haider\Desktop\GameMode\record
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Result: `build\Release\ClipBack.exe` (single, statically linked executable, no DLLs needed).

For Visual Studio 2026 use the generator name it reports (`cmake --help` lists it), e.g.
`-G "Visual Studio 18 2026"`.

Alternative with Ninja (faster incremental builds), from the same prompt:

```bat
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
```

Or simply run `build.bat` in the project root.

## Build (Visual Studio IDE)

1. File → Open → Folder… → select the project folder (CMake support opens it directly), **or**
   run the first `cmake -S . -B build ...` command above and open `build\ClipBack.sln`.
2. Select the *Release | x64* configuration.
3. Build → Build All.

## Installer

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\ClipBack.iss
```

Output: `dist\ClipBack-Setup.exe` (Publisher: FLOW POINT). If you built with Ninja,
pass the exe folder: `ISCC.exe /DBuildDir=..\build-ninja installer\ClipBack.iss`.

## Files produced at runtime

| Path | Purpose |
|------|---------|
| `%LOCALAPPDATA%\ClipBack\config.json` | settings (a `config.json` next to the exe switches to portable mode) |
| `%LOCALAPPDATA%\ClipBack\logs\clipback.log` | log, rotated at 2 MB (`clipback.1.log` … `.3.log`) |
| `%USERPROFILE%\Videos\ClipBack\ClipBack_YYYY-MM-DD_HH-MM-SS.mp4` | saved replays |

## Test checklist

1. **Build without errors** – `cmake --build build --config Release` ends with `0 Error(s)`.
2. **Run** `build\Release\ClipBack.exe` – the window opens on first launch, a tray icon appears.
3. **GPU detection** – Diagnostics tab lists the GPU with vendor and VRAM.
4. **Encoder detection** – Diagnostics lists e.g. `NVIDIA H264 Encoder MFT [NVIDIA, hardware]`, `AMDh264Encoder`, `Intel® Quick Sync Video H.264 Encoder MFT`, plus `H264 Encoder MFT [Microsoft, software]`.
5. **Start replay** – click *Start Replay* (or leave *Start replay automatically* on). The status turns green and the Encoder row shows the selected hardware encoder with *(hardware)*.
6. **Wait 60 seconds** – the *Buffered* row climbs to ~60 s and stops growing; *Buffer memory* stays around `bitrate × 60 s / 8` (≈ 90 MB at 12 Mbps).
7. **Press F10** while a game (or any app) is in the foreground – a toast *"Saving last 60 seconds..."* followed by *"Replay saved"* appears.
8. **Valid MP4** – open `Videos\ClipBack\ClipBack_*.mp4` in Windows Media Player / VLC: H.264 video + AAC audio, correct length. `ffprobe` (if you have it) reports `h264 (High)` and `aac (LC)`.
9. **Replay continues** – the status remains *Recording*; pressing F10 again after a few seconds produces a second file.
10. **Memory / stability** – leave it running for an hour in Task Manager: RAM stays flat (buffer size + ~30 MB), CPU stays at 0–2 % with a hardware encoder, no handle growth. Stop/Start several times and change settings while recording – no crash, restart is automatic on capture loss (display change).

For debugging leaks use the *Debug* configuration: the static CRT debug heap reports leaks in the
Output window when the process exits.
