# WebcamUpscaler

### Current version: V1.0.1

WebcamUpscaler is an experimental, AI-developed GPU camera processing application originally created to work around limitations I encountered with Canon EOS Webcam Utility.

It takes a camera input, processes and upscales it on the GPU, optionally interpolates additional frames, and exposes the processed output directly to OBS through a native OBS source plugin.

## Why I Made This

I use Canon EOS Webcam Utility, but wanted more control over the resulting video feed — particularly resolution, GPU processing, frame interpolation and OBS integration.

Instead of replacing the Canon camera interface, WebcamUpscaler adds another processing stage:

Camera / EOS Webcam Utility  
↓  
WebcamUpscaler  
↓  
GPU processing + upscaling  
↓  
Optional 2x frame interpolation  
↓  
Shared GPU texture  
↓  
OBS plugin  
↓  
OBS Studio

The preview window and OBS output are independent, so the application can display a relatively small preview while providing a full-resolution output such as 3840x2160 to OBS.

## AI-Developed Project

I am not an experienced C++ programmer.

WebcamUpscaler has therefore been developed substantially using OpenAI ChatGPT.

AI has handled a large portion of:

- C++ implementation
- Windows Media Foundation capture
- Direct3D GPU processing
- GPU texture sharing
- OBS/libobs integration
- CMake configuration
- compiler and linker debugging
- installer development
- documentation

The development process has effectively been:

**Idea → AI implementation → build → test on real hardware → report problems → AI modifies code → repeat**

The project is partly an experiment in seeing how far AI-assisted development can allow someone without extensive programming experience to build relatively low-level Windows multimedia software.

## Features

- GPU-accelerated processing
- Canon EOS Webcam Utility support
- Windows Media Foundation capture
- 4K output
- Experimental 2x frame interpolation
- Native OBS source
- Shared GPU texture transport
- No OBS Window Capture required
- Independent preview and output resolution
- Standalone Windows installer
- Windows x64

## Installation

Go to **Releases** on the right side of this page.

Download:

`WebcamUpscaler-V1.0.1-Setup-x64.exe`

Run the installer and follow the instructions.

You do **not** need Visual Studio, CMake, Git or the source code to use the release build.

## Using It With OBS

1. Install WebcamUpscaler.
2. Open WebcamUpscaler.
3. Select your camera input.
4. Select your desired output resolution.
5. Configure frame interpolation if wanted.
6. Start processing.
7. Open OBS.
8. Add a new source.
9. Select **FSR Camera**.

OBS receives the processed GPU output directly instead of capturing the preview window.

## About Frame Generation

The current 2x frame-generation option is experimental camera frame interpolation.

It is **not native AMD FSR Frame Generation**.

Normal camera footage does not provide the motion vectors, depth buffers, camera jitter and other rendering information normally available to FSR inside a game engine.

## Experimental Software

This is an experimental project and much of the implementation is AI-generated.

Hardware, camera, driver and OBS compatibility may vary. Bugs should be expected.

## Disclaimer

WebcamUpscaler is not affiliated with Canon, AMD, Microsoft, OpenAI or the OBS Project.

Canon EOS Webcam Utility is developed by Canon. OBS Studio is developed by the OBS Project. AMD FidelityFX and FSR are AMD technologies.
