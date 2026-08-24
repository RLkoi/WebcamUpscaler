# WebcamUpscaler V1.0.1 — Real AMD FSR 3 build

WebcamUpscaler takes a Windows Media Foundation camera source (including Canon EOS Webcam Utility), sends each frame through AMD's **real FidelityFX FSR 3 upscaler on DirectX 12**, then publishes the processed result to OBS through the native shared-GPU-texture source plugin.

## What changed in this build

The old GPU bilinear/linear scaler has been replaced by an actual FidelityFX API dispatch:

```text
EOS / Media Foundation frame
        -> DX12 input texture
        -> ffxDispatch (FSR 3.x provider)
        -> DX12 FSR output texture
        -> D3D11On12 interop
        -> shared GPU texture
        -> OBS WebcamUpscaler source
```

The application runtime-loads FidelityFX SDK v1.1.4's signed `amd_fidelityfx_dx12.dll`. At startup WebcamUpscaler queries the installed upscaler providers and **explicitly selects an FSR 3.x provider** rather than silently accepting FSR 4 when using a newer SDK.

If the local FidelityFX runtime does not report an FSR 3 provider, initialization fails with an error instead of falling back to the old scaler.

## Important webcam limitation

FSR 3 Super Resolution is a temporal game upscaler. A game normally provides current color, depth, motion vectors, camera jitter, exposure and other renderer information. EOS Webcam Utility only gives WebcamUpscaler a finished color image.

For camera use this project therefore uses an **FSR 3 compatibility input**:

- real camera color frame
- flat synthetic depth
- zero motion vectors
- zero render jitter
- automatic exposure

The work is genuinely executed by AMD FSR 3, but it does not have the same motion information available to a game-engine integration. Fast-moving camera subjects may therefore show temporal artifacts. This is the main area for future optical-flow work.

## 2x frame interpolation

The `2x FG` option is still the existing GPU midpoint interpolation between two FSR-upscaled frames. It is **not AMD FSR 3 Frame Generation**. It remains labelled experimental for that reason.

## Local AMD SDK

Put the AMD FidelityFX SDK inside:

```text
FSR3_SDK\
```

This version targets the AMD FSR SDK 2.x layout and expects at least:

```text
FSR3_SDK\Kits\FidelityFX\api\include\ffx_api.h
FSR3_SDK\Kits\FidelityFX\api\include\dx12\ffx_api_dx12.h
FSR3_SDK\Kits\FidelityFX\upscalers\include\ffx_upscale.h
FSR3_SDK\FidelityFX-SDK-v1.1.4.zip
```

The DLL search in the build scripts is recursive, so the exact signed-binary subfolder is allowed to vary by SDK release.

## Build in Visual Studio

Open:

```text
Fsr3VirtualCamera.sln
```

and build x64. The project runs `Copy-FidelityFX.cmd` after building, which copies the AMD loader and upscaler DLLs beside the application automatically.

When it initializes successfully, the status line reports the FSR provider name returned by FidelityFX, e.g. an FSR 3.1.x provider.

## OBS

The OBS plugin remains a native shared-texture source. Build/install it with the provided build system, then in OBS choose:

```text
Sources -> + -> WebcamUpscaler
```

OBS receives the selected full output resolution directly from the GPU. The on-screen preview size does not determine OBS resolution.

## Standalone installer

Run:

```bat
Build-Installer.cmd
```

from a Visual Studio developer environment. The builder verifies that the local FidelityFX headers and signed DX12 DLLs exist, builds the app and OBS plugin, then embeds all required payload files into:

```text
dist\WebcamUpscaler-V1.0.1-Setup-x64.exe
```

The target PC only needs that installer. It installs:

- `WebcamUpscaler.exe`
- `amd_fidelityfx_dx12.dll`
- the native OBS source plugin and locale data
- a Start Menu shortcut
- an Apps & Features uninstall entry

No WiX is used.

## AI-developed project

WebcamUpscaler is substantially AI-developed with OpenAI ChatGPT. The project exists because its creator wanted to bypass limitations encountered with Canon EOS Webcam Utility without already being an experienced C++/Windows graphics programmer. The practical development loop has been: idea -> generated implementation -> real Windows/EOS/OBS test -> error report -> code revision.

## Notes

This is experimental software. AMD FSR, FidelityFX, Canon EOS Webcam Utility and OBS are technologies/products of their respective owners. WebcamUpscaler is not affiliated with AMD, Canon, Microsoft, OpenAI or the OBS Project.


## FidelityFX SDK v1.1.4 ZIP setup

Put the untouched `FidelityFX-SDK-v1.1.4.zip` directly in `FSR3_SDK/`. Do not manually extract it. Visual Studio runs `Prepare-FidelityFX.cmd` before compiling, extracts the SDK into `FSR3_SDK/_extracted/`, compiles against `ffx-api/include` and `sdk/include`, and copies `PrebuiltSignedDLL/amd_fidelityfx_dx12.dll` beside `WebcamUpscaler.exe`.


## FSR diagnostic
The main window contains a live FidelityFX diagnostic. `ACTIVE` is shown only after the real `ffxDispatch` call returns `FFX_API_RETURN_OK`. It also shows the selected provider, successful dispatch count, last FidelityFX result, and input/output dimensions.

## Build installer from Visual Studio
Open `Fsr3VirtualCamera.sln`. In Solution Explorer, right-click **WebcamUpscaler Installer** and choose **Build**. This runs the complete Release build, OBS plugin build and native installer packaging. The output is `dist\WebcamUpscaler-V1.0.1-Setup-x64.exe`. The installer project is intentionally excluded from normal **Build Solution** so it does not recursively package every time you compile the app.
