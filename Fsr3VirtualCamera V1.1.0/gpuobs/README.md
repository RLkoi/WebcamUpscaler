# WebcamUpscaler V1.0.1

**An experimental, AI-developed GPU camera-processing application originally created to work around limitations I encountered with Canon EOS Webcam Utility.**

WebcamUpscaler takes a camera source such as Canon EOS Webcam Utility, processes it on the GPU, upscales the image, optionally interpolates extra frames, and sends the full-resolution result directly to OBS through a native OBS source plugin.

## Why I Made It

I wanted more control over Canon EOS Webcam Utility, especially output resolution, processing, frame interpolation, and OBS integration.

Pipeline:

Canon EOS Camera
-> EOS Webcam Utility
-> Windows Media Foundation
-> WebcamUpscaler
-> GPU Processing
-> Upscaling
-> Optional 2x Frame Interpolation
-> S













hared GPU Texture
-> Native OBS Source
-> OBS Studio

The preview window is independent from the OBS output resolution, so OBS can receive 3840x2160 even if the preview is much smaller.

## AI-Developed

I am not an experienced C++ developer, so this project has been developed substantially using OpenAI ChatGPT.

AI has been used for:

- C++ implementation
- application architecture
- Windows Media Foundation integration















- Direct3D GPU processing
- shared GPU texture transport
- OBS/libobs plugin development
- CMake setup
- compiler and linker debugging
- installer development
- documentation

Development process:

Idea / limitation











-> AI implementation
-> Build on real hardware
-> Test with EOS, GPU and OBS
-> Report errors and behaviour
-> AI diagnoses and modifies the code
-> Repeat

## Features

-








 Windows x64
- GPU-accelerated processing
- Canon EOS Webcam Utility support
- Windows Media Foundation capture
- High-resolution output including 4K
- Experimental 2x frame interpolation
- Native OBS source plugin
-






 Shared GPU texture output
- No OBS Window Capture required
- Preview resolution independent of OBS output
- Standalone installer

## Installation







Download:

WebcamUpscaler-V1.0.1-Setup-x64.exe

from the GitHub Releases page and run it.







## OBS

Start WebcamUpscaler and begin processing.

In OBS:







Sources -> + -> FSR Camera

## Frame Generation






The current 2x mode is experimental camera-frame interpolation.

It is not native AMD FSR Frame Generation. Webcam footage does not naturally provide the motion vectors, depth buffers, camera jitter, and other renderer information available inside a game engine.

## Experimental Software






WebcamUpscaler is experimental and substantially AI-generated. Bugs and hardware-specific compatibility issues may occur.

## Disclaimer





WebcamUpscaler is not affiliated with AMD, Canon, Microsoft, OpenAI, or the OBS Project.

## Version





**WebcamUpscaler V1.0.1**
