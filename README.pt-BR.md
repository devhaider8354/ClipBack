# ClipBack

[English](README.md) | **Português (Brasil)** | [العربية](README.ar.md)

Gravador de replay instantâneo para jogos no Windows. Ele mantém os últimos 30, 60 ou 120 segundos de jogo na memória e salva um MP4 quando você pressiona um atalho (F10 por padrão) — semelhante ao NVIDIA Instant Replay, mas independente da marca da GPU.

## Recursos

- C++20 nativo / Win32; executável único, sem Electron, WebView ou Python.
- Captura com Windows Graphics Capture ou Desktop Duplication, sem injeção em jogos e sem hooks de teclado.
- Codificação H.264 por hardware com NVIDIA NVENC, AMD AMF ou Intel Quick Sync; alternativa por software quando necessário.
- Buffer circular de pacotes codificados na RAM: não grava no SSD até você salvar o replay.
- Áudio do jogo por WASAPI e microfone opcional, mixados e codificados em AAC.
- Atalhos globais, ícone na bandeja, notificações, inicialização automática e gravação contínua de partida.
- Interface em inglês, português (Brasil) e árabe, incluindo layout RTL para árabe.

## Download e uso

Baixe `ClipBack-Setup.exe` na página de Releases. Ele requer Windows 10 1903 ou posterior (ou Windows 11), em x64.

1. Abra o ClipBack.
2. Inicie ou deixe ativada a opção **Iniciar replay automaticamente ao abrir**.
3. Jogue normalmente.
4. Pressione **F10** para salvar os últimos 60 segundos em `Vídeos\ClipBack`.

Na aba **Configurações**, escolha **Português (Brasil)** no campo **Idioma**. A escolha é persistida no arquivo de configuração.

## Compilar

Requisitos:

- Windows 10 ou 11.
- Visual Studio 2022 (ou posterior) com **Desenvolvimento para desktop com C++**.
- MSVC x64/x86, Windows SDK 10.0.19041 ou posterior e ferramentas CMake para Windows.
- CMake 3.21 ou posterior.

Não há bibliotecas de terceiros para instalar.

No **x64 Native Tools Command Prompt for VS 2022**:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

O executável é gerado em `build\Release\ClipBack.exe`. Também é possível usar Ninja:

```bat
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
```

Consulte o [guia completo de compilação](BUILD.md), incluindo como gerar o instalador e o checklist de testes.

## Configuração

O arquivo fica em `%LOCALAPPDATA%\ClipBack\config.json`. Um `config.json` ao lado do executável ativa o modo portátil.

```json
{
  "replay_duration": 60,
  "resolution": "same",
  "fps": 60,
  "bitrate": 12000,
  "encoder": "auto",
  "hotkey": { "modifiers": 0, "vk": 121, "display": "F10" },
  "microphone_enabled": false,
  "audio_mode": "mixed",
  "microphone_device": "",
  "output_device": "",
  "save_directory": "",
  "start_with_windows": false,
  "auto_start_replay": true,
  "start_minimized": true,
  "capture_cursor": true,
  "clip_sound_enabled": true,
  "hide_capture_border": true,
  "monitor_index": 0,
  "exclude_window_from_capture": true,
  "language": "pt-BR"
}
```

Com `hide_capture_border` ativado, o ClipBack prioriza Desktop Duplication para evitar a borda amarela da captura. Caso esse modo não esteja disponível, ele usa Windows Graphics Capture como alternativa.

`audio_mode` define as faixas AAC do MP4: `mixed` mantém o comportamento atual (PC + microfone em uma faixa), `separate` cria faixas independentes de PC e microfone, e `three` cria as três faixas: mix, PC e microfone. Fontes silenciosas ou indisponíveis permanecem como faixas AAC silenciosas para preservar sincronização em editores de vídeo.

## Arquitetura

O caminho de vídeo permanece na GPU: o DWM fornece uma textura BGRA, o processador de vídeo D3D11 converte para NV12 e um Media Foundation Transform codifica H.264. Os pacotes codificados vão para um buffer circular na RAM. Ao salvar, o ClipBack só cria um snapshot desses pacotes e os multiplexa para MP4 em segundo plano.

O áudio do jogo e o microfone usam WASAPI. O mixer usa o mesmo relógio QPC da captura para manter áudio e vídeo sincronizados.

Para a análise técnica completa, decisões de arquitetura e limitações conhecidas, consulte o [README em inglês](README.md).

## Licença

MIT © 2026 FLOW POINT — desenvolvido por Haider.
