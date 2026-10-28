# ClipBack

[![Build](https://github.com/devhaider8354/ClipBack/actions/workflows/build.yml/badge.svg)](https://github.com/devhaider8354/ClipBack/actions/workflows/build.yml)

**English** | [العربية](README.ar.md)

Instant-replay recorder for Windows games. It keeps the last 30/60/120 seconds of
gameplay in memory and writes them to an MP4 when you press a hotkey (default **F10**) – the same idea
as NVIDIA Instant Replay, but vendor-independent (NVIDIA, AMD, Intel) and with no third-party runtime.

* Native C++20 / Win32, single 1-2 MB executable, no DLLs, no Electron/WebView, no Python.
* Windows Graphics Capture + Direct3D 11 video processor + hardware H.264 (NVENC / AMF / Quick Sync) through Media Foundation.
* GPU-only frame path: the captured frame is converted and encoded without ever being copied to system memory.
* Encoded ring buffer in RAM (≈ 90 MB for 60 s at 12 Mbps), zero disk writes until you save.
* Game audio (WASAPI loopback) + optional microphone, mixed and AAC-encoded, A/V in sync.
* Global hotkey via `RegisterHotKey` (no keyboard hook), tray icon, toast notifications, autostart option.
* Official OS APIs only – no injection, no hooks inside the game, anti-cheat safe.
* Full-match recording: one hotkey records the whole match straight to disk next to the replay buffer.
* Bilingual UI: English and Arabic (right-to-left layout), switchable in Settings.

## Download

Get `ClipBack-Setup.exe` from the [Releases](../../releases) page (Windows 10 1903+ / Windows 11, x64).
Start a game, play, and press **F10** – the last 60 seconds are saved to `Videos\ClipBack`.

See **BUILD.md** for build instructions and the test checklist.

---

## 1. Architecture analysis

### 1.1 Where the cost is

For a "last-60-seconds" recorder the only per-frame work that can hurt the game is:

| Stage | Naive approach | Cost | Chosen approach | Cost |
|-------|----------------|------|-----------------|------|
| Capture | GDI `BitBlt` / `GetDIBits` | 1080p60 = 500 MB/s of CPU copies, stalls the GPU | **Windows Graphics Capture** (DWM hands us the composed frame as a D3D11 texture) | one GPU-side copy done by DWM, 0 CPU |
| Colour conversion | CPU BGRA→NV12 | ~10-20 % of a core at 1080p60 | **D3D11 `VideoProcessorBlt`** (fixed-function video engine, also scales) | ~0 CPU, negligible GPU |
| Encoding | x264 on CPU | 1-4 cores | **Hardware MFT** (NVENC / AMF / QSV) fed with the NV12 texture through `IMFDXGIDeviceManager` | dedicated silicon, 0 CPU |
| Buffering | write raw/encoded frames to disk | constant SSD writes, I/O jitter | **RAM ring buffer of encoded packets** | 90 MB, no I/O |
| Saving | re-encode / transcode | seconds of CPU | **Pass-through mux** (`IMFSinkWriter` with identical in/out types) | < 1 s, one low-priority thread |
| Hotkey | `WH_KEYBOARD_LL` hook | code runs on every keystroke system-wide, looks like a key logger to anti-cheat | `RegisterHotKey` | 0 |

Everything else (UI, status refresh, watchdog) is idle-time work on 1 s timers.

### 1.2 Capture: Windows Graphics Capture vs Desktop Duplication vs hooks

| | Windows Graphics Capture | DXGI Desktop Duplication | Game hooks (OBS game-capture / ShadowPlay) |
|---|---|---|---|
| Fullscreen exclusive | yes on Windows 10 2004+ (DWM flips the game's swap chain) | yes | yes |
| Borderless / windowed | yes | yes | yes |
| DX9/11/12/Vulkan/OpenGL | all (works at the compositor level) | all | per-API hooks |
| Anti-cheat | official API, no injection | official API | **injects a DLL – banned by many anti-cheats** |
| Cost | DWM copies the frame once on the GPU | same | zero-copy, but only with injection |
| Availability | Windows 10 1903+ | Windows 8+ | – |

**Decision:** WGC monitor capture as the primary path, Desktop Duplication as an automatic fallback
(old Windows builds, or WGC failing to start). Injection is rejected outright. Monitor capture (rather
than window capture) is used because it covers exclusive fullscreen and every window mode with one
code path and never loses the target when the game recreates its window.

Known WGC limitations and how they are handled:

* **Yellow border / cursor** – `IsBorderRequired(false)` and `IsCursorCaptureEnabled` are applied when the
  OS supports them (wrapped in `try/catch`, older builds ignore them).
* **Resolution change** (game switches display mode) – `Direct3D11CaptureFramePool::Recreate` on
  `ContentSize` change; the video processor re-creates itself for the new input size and keeps
  scaling to the fixed encoder size, so the encoder never has to be re-negotiated.
* **HDR displays** – WGC returns SDR-tonemapped BGRA when the pool format is `B8G8R8A8UIntNormalized`, so HDR games record correctly.
* **Protected content (DRM video)** – appears black by OS design; unrelated to games.
* **The app's own window** is excluded with `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` while a replay session is running (so it never appears in the video; it is a normal window again when stopped).
* **Frames only arrive when the screen changes** – for games that is every frame; for a static screen the encoder simply gets fewer frames, timestamps stay correct.

### 1.3 Encoding: Media Foundation vs vendor SDKs vs FFmpeg

| | Media Foundation hardware MFTs | NVENC/AMF/QSV SDKs directly | FFmpeg |
|---|---|---|---|
| Vendors | NVIDIA, AMD, Intel with one code path (`MFTEnumEx` + vendor id) | three SDKs, three code paths | all, but via a 30-80 MB DLL set |
| D3D11 input | yes (`MF_SA_D3D11_AWARE`, DXGI device manager) | yes | needs `hwupload`/`d3d11va` plumbing |
| MP4 muxer | built in (`IMFSinkWriter`) | none | yes |
| AAC | built in | none | yes |
| Licensing | ships with Windows | vendor EULAs | LGPL/GPL obligations, redistribution rules |
| Software fallback | Microsoft H.264 MFT | none | x264 (GPL) |

**Decision:** Media Foundation. It gives hardware H.264 on all three vendors, the CPU fallback, AAC
and the MP4 muxer from components that are already on every Windows 10/11 machine, so the executable
stays self-contained and there is nothing to license. FFmpeg would only add value for codecs this
tool does not need.

Encoder selection order (`Auto`): hardware encoder of the GPU that drives the captured monitor →
NVENC → AMF → QSV → Microsoft software H.264 (only if no hardware encoder exists). An explicit choice
that is not present falls back in the same order with a warning in the log.

Encoder configuration: CBR at the chosen bitrate, GOP = 2 s, **no B-frames** (pts == dts, lower
latency, simpler muxing), low-latency mode, High profile (Main/Baseline fallback).

### 1.4 Ring buffer: RAM vs disk vs "hybrid"

The buffer stores **encoded** packets, so its size is simply `bitrate × duration`:

| Bitrate | 30 s | 60 s | 120 s |
|---------|------|------|-------|
| 8 Mbps | 30 MB | 60 MB | 120 MB |
| 12 Mbps (default) | 45 MB | **90 MB** | 180 MB |
| 20 Mbps | 75 MB | 150 MB | 300 MB |

Even the worst case (300 MB) is a fraction of what a game uses and far below what any disk-backed
scheme would cost in I/O jitter, so a hybrid RAM/disk design would only add complexity and SSD wear
without lowering memory in a way anyone would notice. The buffer is therefore RAM-only, but built the
way a segment scheme would be:

* packets are reference-counted (`shared_ptr<const vector<uint8_t>>`); a save **snapshots handles, not bytes**, so saving never copies the 90 MB and never blocks the encoder;
* trimming drops whole GOPs from the front, so the buffer always starts on a keyframe and every saved file is decodable from frame 0;
* audio packets are trimmed against the video window.

Memory headroom beyond the buffer: 8 NV12 textures (1080p ≈ 3 MB each) in VRAM, one 40 ms WASAPI
buffer per audio source, a few KB of audio FIFO.

### 1.5 Threads and waiting

| Thread | Wakes on | Never |
|--------|----------|-------|
| WGC frame callback (thread pool) | DWM delivering a frame | polls |
| Duplication thread (fallback) | `AcquireNextFrame` kernel wait, 100 ms timeout | spins |
| Encoder thread | condition variable signalled by MFT `METransformNeedInput` / `METransformHaveOutput` events (`IMFAsyncCallback`) or by a queued frame | polls the MFT |
| WASAPI threads (game, mic) | WASAPI event handle (20 ms timeout for silent loopback) | spins |
| Audio mixer | 10 ms timed condition-variable wait (needed to pace the audio clock) | – |
| Watchdog | 1 s timed wait | – |
| Save thread | created per save, `THREAD_PRIORITY_BELOW_NORMAL`, exits when done | touches the live pipeline |
| UI | message loop, 1 s timer only while the window is visible | – |

Back-pressure: the encoder input queue holds at most 2 frames; if the encoder falls behind the oldest
frame is dropped so the capture callback never blocks DWM. Frame pacing drops capture frames above the
configured FPS (e.g. 144 Hz monitor → 60 FPS recording) before any GPU work is issued for them.

### 1.6 A/V sync

Video timestamps come from `Direct3D11CaptureFrame::SystemRelativeTime` (QPC based). The audio mixer
runs on the same QPC clock: every tick it produces exactly the number of samples that wall-clock time
has elapsed since the session started, padding with silence when the game is quiet (loopback delivers
nothing during silence) and discarding backlog if a device drifts ahead. AAC blocks therefore carry
timestamps that line up with the video by construction.

---

## 2. Module map

```
src/
  common/    Common.h (clock, RAII helpers), StringUtils
  logging/   Logger – thread-safe file log with size rotation
  settings/  Json (in-house parser/serializer), Settings (config.json)
  system/    Paths, SystemInfo (GPU/DX/encoders/audio/monitors), Startup (Run key)
  capture/   D3DDevice, ICaptureSource, WgcCapture, DuplicationCapture
  encoder/   VideoConverter (BGRA→NV12 on GPU), VideoEncoder (MF H.264 MFT), H264Utils
  buffer/    RingBuffer – encoded packet ring, GOP-aligned trimming, snapshot
  audio/     WasapiCapture (loopback + mic), AudioMixer, AacEncoder
  muxer/     Mp4Writer – pass-through MP4 via IMFSinkWriter
  hotkeys/   HotkeyManager – RegisterHotKey
  engine/    ReplayEngine – wires everything, watchdog, background save
  ui/        MainWindow (Replay / Settings / Diagnostics tabs), TrayIcon
  main.cpp   single instance, MF/WinRT init, message loop
resources/   icon, manifest (DPI, common controls), version info
installer/   Inno Setup script
```

Data flow:

```
DWM ──WGC──▶ BGRA texture ──VideoProcessorBlt──▶ NV12 texture ──▶ H.264 MFT (NVENC/AMF/QSV) ──▶ packets ─┐
WASAPI loopback ─┐                                                                                          ├─▶ RingBuffer (last N s)
WASAPI mic ──────┴──▶ AudioMixer (48 kHz stereo, QPC clock) ──▶ AAC MFT ──▶ packets ────────────────────────┘
                                                                                                        F10 ▼
                                                                          snapshot ──▶ IMFSinkWriter ──▶ Videos\ClipBack\*.mp4
```

---

## 3. Configuration (`config.json`)

```json
{
  "replay_duration": 60,
  "resolution": "same",          // "same" | "1080p" | "720p"
  "fps": 60,                     // 60 | 30
  "bitrate": 12000,              // kbps
  "encoder": "auto",             // "auto" | "nvenc" | "amf" | "qsv" | "cpu"
  "hotkey": { "modifiers": 0, "vk": 121, "display": "F10" },
  "microphone_enabled": false,
  "microphone_device": "",       // WASAPI endpoint id, "" = default
  "output_device": "",           // game audio endpoint id, "" = default
  "save_directory": "",          // "" = %USERPROFILE%\Videos\ClipBack
  "start_with_windows": false,
  "auto_start_replay": true,
  "start_minimized": true,
  "capture_cursor": true,
  "monitor_index": 0,
  "exclude_window_from_capture": true,  // keep the ClipBack window out of the video while recording
  "language": "ar"                       // "ar" or "en" (defaults to the Windows display language)
}
```

Keyboard shortcuts in the main window: **Ctrl+S** save replay, **Ctrl+O** open the replay folder, **Ctrl+Q** exit
(closing the window with X only hides it to the tray).

Location: `%LOCALAPPDATA%\ClipBack\config.json`. Put a `config.json` next to `ClipBack.exe`
for portable mode. Comments are allowed in the file.

## 4. Error handling

Every component returns a clear message on failure (capture, encoder, audio, muxing, hotkey, device
init). Fatal pipeline errors stop the session, show a toast, and the UI retries up to three times
(covers display-mode changes and driver resets). Audio failures degrade to video-only instead of
stopping the replay. Everything is written to `%LOCALAPPDATA%\ClipBack\logs\clipback.log`
(rotated at 2 MB, 3 backups); the Diagnostics tab has an *Open log file* button.

---

## License

[MIT](LICENSE.txt) © 2026 FLOW POINT – developed by Haider ([flowp.uk](https://flowp.uk))
