# NotITG Linux Compatibility Fix (`winmm.dll` Proxy)

A lightweight 32-bit proxy DLL that resolves two long-standing, game-breaking issues when running **NotITG** (closed-source StepMania 3.95 / OpenITG derivative) on **Linux** via **Wine** or **Proton**:

1. **High Refresh Rate Judder** on high-refresh monitors (144Hz, 240Hz, 360Hz+).
2. **Opaque Black Box Backgrounds (`#000000`)** on **Transparent ActorFrameTextures (AFTs)**.

---

## Table of Contents
- [How to Use (Quick Start)](#how-to-use-quick-start)
  - [Installation Steps](#installation-steps)
  - [Compiling from Source](#compiling-from-source)
- [How It Works & Root Cause Deep-Dive](#how-it-works--root-cause-deep-dive)
  - [Issue 1: Audio Driver Micro-stuttering & Input Jitter](#issue-1-audio-driver-micro-stuttering--input-jitter)
    - [The Problem](#the-problem)
    - [The Fix: Continuous Phase-Locked Loop (PLL) Audio Clock](#the-fix-continuous-phase-locked-loop-pll-audio-clock)
  - [Issue 2: Broken Transparent ActorFrameTextures (Black Boxes)](#issue-2-broken-transparent-actorframetextures-black-boxes)
    - [The Problem](#the-problem-1)
    - [The Fix: GDI `ChoosePixelFormat` Runtime Hook](#the-fix-gdi-choosepixelformat-runtime-hook)
- [Verification & Logs](#verification--logs)
- [License & Credits](#license--credits)

---

## How to Use (Quick Start)

### Installation Steps

1. **Copy `winmm.dll`**:
   Place the compiled `winmm.dll` directly into the `Program/` folder of your NotITG installation (in the same directory as `NotITG-v4.9.1.exe`):
   ```text
   NotITG/
   └── Program/
       ├── NotITG-v4.9.1.exe
       ├── winmm.dll           <-- Place here
       └── ...
   ```

2. **Set the DLL Override in Wine / Proton**:
   Because `winmm.dll` is a core Windows system library, Wine must be instructed to prefer the local version (*native*) over the built-in Wine version:
   
   - **If launching via Steam (Recommended)**:
     Right-click **NotITG** in your Steam Library -> **Properties** -> **General** -> **Launch Options**, and add:
     ```bash
     WINEDLLOVERRIDES="winmm=n,b" %command%
     ```
   - **If launching via standalone Wine / Lutris / Bottles**:
     Set the environment variable:
     ```bash
     export WINEDLLOVERRIDES="winmm=n,b"
     wine Program/NotITG-v4.9.1.exe
     ```
     *(Or open `winecfg` -> **Libraries** tab -> type `winmm` -> Add -> Set to **Native then Builtin**)*.

---

### Compiling from Source

If you want to build `winmm.dll` yourself from the source code on Linux, install the 32-bit MinGW toolchain:

#### On Arch / CachyOS / Manjaro:
```bash
sudo pacman -S mingw-w64-gcc
```

#### On Ubuntu / Debian / Pop!_OS:
```bash
sudo apt install gcc-mingw-w64-i686
```

#### Build:
```bash
make
```
This produces a 32-bit `winmm.dll` ready for installation.

---

## How It Works & Root Cause Deep-Dive

Because NotITG is closed-source, these fixes are injected entirely at runtime through DLL proxying and memory hooking without modifying the executable on disk.

```
+-----------------------------------------------------------------------------------+
|                               NotITG-v4.9.1.exe                                   |
+-----------------------------------------------------------------------------------+
         |                                                 |
         | Imports GDI32.dll!ChoosePixelFormat             | Imports WINMM.dll!waveOut*
         v                                                 v
+-----------------------------------------------------------------------------------+
|                            winmm.dll (Proxy Hook)                                 |
+-----------------------------------------------------------------------------------+
|  [Alpha Framebuffer Fix]                         |  [Smooth Audio PLL Clock]      |
|  IAT Hook: GDI32.dll                             |  Wraps: waveOutGetPosition     |
|  Forces 32-bit RGBA (8-bit alpha backbuffer)     |  Phase-Locked Loop servo clock |
|  Preserves transparent AFTs (glCopyTexSubImage)  |  Judder-free 360Hz+ scrolling  |
+-----------------------------------------------------------------------------------+
         |                                                 |
         v                                                 v
  Host GPU / Mesa / NVIDIA GLX                     Real Wine/Proton audio driver
```

---

### Issue 1: Audio Driver Micro-stuttering & Input Jitter

#### The Problem
In StepMania 3.95 and OpenITG (the codebase NotITG is branched from), visual arrow positioning, playfield scrolling, and input polling are strictly tied to the audio clock. In every frame of `RageDisplay`, the engine calls `RageSoundManager`, which queries the audio device position via the Windows Multimedia API (`waveOutGetPosition`).

Under native Windows, audio drivers continuously report sub-millisecond audio position updates. However, on Linux under Wine/Proton:
- Wine's audio backends (PulseAudio / PipeWire / ALSA) update `waveOutGetPosition` in coarse, discrete buffer blocks (typically every 10–20 ms, equivalent to ~50–100Hz).
- On a high-refresh-rate monitor, the game engine renders several display frames **during a single audio quantum**.
- To the game engine, audio playback appears completely frozen for 3–5 consecutive video frames, followed by a sudden jump forward when the next audio buffer slice completes.

**Result**: Even though the game reports running at X FPS, the arrows noticeably jitter, judder, and stutter across the screen, and accuracy timing windows feel unstable.

#### The Fix: Continuous Phase-Locked Loop (PLL) Audio Clock
The proxy DLL intercepts `waveOutOpen`, `waveOutWrite`, `waveOutPause`, `waveOutRestart`, `waveOutReset`, and `waveOutGetPosition`.

Instead of blindly returning Wine's stepped audio counter:
1. It reads high-precision hardware timestamps via `QueryPerformanceCounter` (sub-microsecond resolution).
2. It interpolates playback continuously between audio buffer deliveries.
3. It implements a **Phase-Locked Loop (PLL) proportional servo loop**:
   $$\text{error} = \text{audio}_{\text{Wine}} - \text{position}_{\text{smooth}}$$
   $$\text{speed} = 1.0 + \left(\frac{\text{error}}{\text{SampleRate}}\right) \times 0.75$$
   - If the smooth clock is slightly lagging behind real audio playback ($\text{error} > 0$), it accelerates by up to $+2\%$.
   - If the smooth clock is slightly ahead ($\text{error} < 0$), it decelerates by up to $-2\%$.
4. It strictly enforces monotonic advancement (the clock never goes backward or freezes).

---

### Issue 2: Broken Transparent ActorFrameTextures (Black Boxes)

#### The Problem
NotITG introduced custom Lua methods for ActorFrameTextures (AFTs), including `EnableAlphaBuffer(true)` and `clearbuffer,1`. Song writers frequently use transparent AFTs to isolate actors (e.g. player judgment, combo, or arrow notes) against a transparent cutout background.

Reverse engineering `NotITG-v4.9.1.exe` revealed how screen-capturing AFTs function:
1. In `RageDisplay_OGL::CreateRenderTarget`, NotITG creates a Win32 render target (`RenderTarget_Win32`).
2. When capturing an AFT with no children, `RenderTarget_Win32::FinishRenderingTo` executes:
   ```c
   glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
   ```
   It captures the contents drawn prior on the **main window backbuffer** directly into the AFT texture.
3. When initializing the window in `LowLevelWindow_Win32::ChooseWindowPixelFormat`, NotITG requests:
   ```cpp
   PixelFormat->cColorBits = p.bpp == 16 ? 16 : 24;
   PixelFormat->cDepthBits = 16;
   // cAlphaBits is left as 0
   ```
4. On native Windows, GPU drivers (such as NVIDIA WGL) routinely grant a 32-bit pixel format with an 8-bit alpha channel (`cAlphaBits = 8`) to the backbuffer even when 24-bit color is requested.
5. Under Wine/Proton on Linux, Wine evaluates the request against native X11/GLX visuals. Because `cColorBits = 24` was requested, Wine scores a 24-bit visual with **0 alpha bits** as the closest match (`Mode: ICD 24 (888) 24 depth 8 stencil`).
6. **The OpenGL Specification trap**:
   According to the OpenGL specification for `glCopyTexSubImage2D` (§4.3.3):
   > *"If the color buffer does not have an alpha component, the alpha component in the destination texture is assigned the value 1.0."*
7. When a modfile clears the background using `clearbuffer,1` (`glClearColor(0, 0, 0, 0)`), the backbuffer stores `RGB = (0, 0, 0)` and drops the alpha channel.
8. When `glCopyTexSubImage2D` copies that region into the AFT's RGBA texture, **OpenGL forces the alpha channel to 1.0 (opaque)**!

**Result**: `(R=0, G=0, B=0, A=1.0)` produces a solid, opaque black rectangle (`#000000`), obscuring everything underneath.

#### The Fix: GDI `ChoosePixelFormat` Runtime Hook
During process initialization (`DLL_PROCESS_ATTACH`), `winmm.dll`:
1. Traverses the PE Import Address Table (IAT) of `NotITG-v4.9.1.exe`.
2. Hooks `GDI32.dll!ChoosePixelFormat` and `GDI32.dll!SetPixelFormat`.
3. When NotITG asks GDI to choose a pixel format:
   - The hook queries all available device context pixel formats via `DescribePixelFormat`.
   - It filters for accelerated ICD OpenGL formats that have `PFD_DRAW_TO_WINDOW`, `PFD_SUPPORT_OPENGL`, and `PFD_DOUBLEBUFFER`.
   - It enforces **`cAlphaBits >= 8`** and `cColorBits >= 24` (selecting a 32-bit RGBA 8:8:8:8 backbuffer with 24-bit depth and 8-bit stencil).
4. With an 8-bit alpha channel physically present on the backbuffer:
   - `glClearColor(0, 0, 0, 0)` clears the backbuffer alpha to `0.0`.
   - `glCopyTexSubImage2D` reads the real alpha channel (`0.0`) from the backbuffer instead of substituting `1.0`.

**Result**: Transparent AFTs render with complete alpha transparency, perfectly matching the appearance on native Windows.

---

## Verification & Logs

The proxy automatically generates a log file named `winmm_smooth.log` in the `Program/` directory upon launch. You can inspect this log to confirm both fixes are active:

```text
[winmm-proxy] Successfully hooked real winmm.dll
[winmm-proxy] DllMain DLL_PROCESS_ATTACH, QPC Freq = 10000000
[winmm-pfd] Hooked ChoosePixelFormat via IAT at 00998064
[winmm-pfd] Hooked SetPixelFormat via IAT at 0099804c
[winmm-pfd] ChoosePixelFormat intercepted!
[winmm-pfd] Wine original choice: 241
[winmm-pfd] Wine format 241 details: flags=0x25, type=0, color=24 (R8 G8 B8 A0), depth=24, stencil=8
[winmm-pfd] Enumerating 480 pixel formats for 32-bit RGBA (alpha >= 8)...
[winmm-pfd] -> SELECTED alpha format 1 (score 10850): flags=0x25, color=32 (A8), depth=24, stencil=8
[winmm-pfd] SetPixelFormat called for format 1
[winmm-pfd] SetPixelFormat result: 1 (LastError=0x0)
[winmm-proxy] waveOutOpen: rate=44100, channels=2, blockAlign=4
[pll] #001: raw=0, reported=0, diff=0.0
...
```

---
