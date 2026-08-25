# WebcamUpscaler

### Current version: V2-X.2.1

## Known Support
Canon EOS 700D

Logitech C920

# Assumed Support
Generic Media Foundation Webcams

Canon EOS Cameras Running EOS Webcam Utility

## Version Naming

WebcamUpscaler does not use conventional semantic versioning.

Its version numbers are based on **how significantly an update changes the processing pipeline**, rather than simply counting releases.

The general format is:

`V<X>[Y].<Z>.<A>`

### X — Pipeline Generation

The first number represents the fundamental generation of WebcamUpscaler's processing pipeline.

A change to this number means the architecture or processing pipeline has changed significantly enough to be considered a new generation.

Examples:

- `V1` — Original WebcamUpscaler pipeline
- `V2` — FidelityFX FSR-based pipeline
- `V3` — Future major pipeline generation

The generation can also advance if the regular update number reaches its rollover point.

### Y — Pipeline Modifier

The optional letter after the generation represents a significant modification to the existing pipeline that is important enough to distinguish it from normal updates, but does not constitute an entirely new pipeline generation.

If no such modification exists, **there is no letter**.

Pipeline modifiers are ranked from most significant to least significant:

`X > Y > Z > B > F`

For example:

`V2X` represents a more substantial modification of the V2 pipeline than `V2B`.

The **V2X designation is specifically reserved for the AMD driver-override version of WebcamUpscaler**, where the V2 FSR integration remains the underlying pipeline but AMD's driver replaces/upgrades the FSR implementation.

### Z — Regular Update

The first number after the decimal represents normal WebcamUpscaler releases.

This includes regular features, fixes, improvements and other updates that do not significantly alter the processing pipeline.

For example:

- `V2.0` — Initial V2 release
- `V2.1` — First regular V2 update
- `V2.2` — Second regular V2 update

If this number reaches **10**, the version rolls into the next pipeline generation rather than continuing indefinitely.

### A — Patch / Small Addition

The final number is used for small patches, additions and minor changes.

These are changes that do not justify a normal version increment.

For example:

- `V2.1.0` — Normal V2.1 release
- `V2.1.1` — Small patch/addition to V2.1
- `V2.1.2` — Another small patch

### Example Version Progression

A possible release history could therefore look like:

`V2.0.0`
→ `V2.1.0`
→ `V2.1.1`
→ `V2.2.0`
→ `V2F.0.0`
→ `V2B.0.0`
→ `V2Z.0.0`
→ `V2Y.0.0`
→ `V2X`
→ `V3.0.0`

This makes the version name itself indicate **how important an update is to WebcamUpscaler's actual processing architecture**, rather than only indicating when it was released.

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
