<img width="2000" height="2000" alt="WebcamUpscaler Logo" src="https://github.com/user-attachments/assets/052e0668-1889-4304-9041-cf07c64e6e02" />


# WebcamUpscaler

### Current Version: V2-XB

WebcamUpscaler is an experimental GPU-accelerated camera processing application that applies AMD FidelityFX Super Resolution to live camera footage.

Unlike a conventional video upscaler, WebcamUpscaler attempts to reconstruct some of the spatial and temporal information that FSR would normally receive directly from a video-game engine.

This system is called:

# AVCO — Adaptive Variable Capture Optimisations

AVCO analyses live camera frames and generates additional information about motion, relative depth, temporal changes and image composition.

This information is passed through a lightweight analysis processor and supplied to the FidelityFX pipeline to better emulate the information FSR would normally receive from a rendered 3D scene.

---

# Support

## Known Support

Tested cameras:

- Canon EOS 700D
- Logitech C920

## Assumed Support

Expected to work:

- Generic Windows Media Foundation webcams
- Canon EOS cameras running EOS Webcam Utility
- Other UVC-compatible webcams exposed through Media Foundation

## Unsupported

No cameras are currently explicitly listed as unsupported.

Compatibility is not guaranteed.

---

# AVCO — Adaptive Variable Capture Optimisations

Modern temporal upscalers such as AMD FidelityFX Super Resolution are primarily designed for video games.

A game engine has access to information that a normal camera does not provide.

This can include information describing:

- how objects are moving
- relative scene structure
- which parts of the image have changed
- which temporal information can be trusted
- exposure
- rendering jitter
- composition/transparency information

A webcam normally provides little more than a sequence of completed image frames.

AVCO attempts to bridge this gap.

Instead of treating the camera image as completely flat video, AVCO performs lightweight analysis of consecutive camera frames and reconstructs useful **spatial and temporal metadata**.

The basic pipeline is:

```text
Camera
  ↓
Captured Frames
  ↓
AVCO Spatial + Temporal Analysis
  ↓
Lightweight Data Processor
  ↓
Motion / Depth / Reactive / Composition Data
  ↓
AMD FidelityFX FSR
  ↓
Upscaled Camera Frame
```

AVCO therefore acts somewhat like a translation layer between camera imagery and the rendering metadata expected by a temporal game upscaler.

AVCO does **not** reconstruct a complete 3D scene.

Instead, it estimates properties of the captured scene that are useful to the temporal upscaler.

---

# AVCO Motion Vectors

Video-game engines normally provide FSR with motion vectors generated during rendering.

A webcam cannot provide these directly.

AVCO therefore estimates motion by comparing consecutive camera frames using a lightweight motion-analysis system inspired by techniques used in video compression.

The image is divided into regions and AVCO searches previous frames for the most likely previous location of each region.

```text
Current Frame
      +
Previous Frame
      ↓
Region / Block Matching
      ↓
Estimated X/Y Movement
      ↓
Motion Confidence
      ↓
FSR Motion Information
```

This allows AVCO to follow moving subjects through the image.

Movement of a person's:

- head
- hands
- body
- hair
- clothing
- other visible objects

can therefore generate corresponding motion information.

AVCO also generates a **motion confidence map** describing how confident the processor is in the estimated movement.

---

# AVCO Relative Depth Estimation

A normal webcam does not provide the depth buffer that would exist inside a game engine.

AVCO instead attempts to derive **relative scene depth** from focus and spatial image information.

Camera lenses naturally contain useful spatial information.

Objects close to the active focus plane generally contain stronger fine detail, while objects outside that plane become progressively softer.

AVCO analyses local image sharpness and focus characteristics to construct an approximate depth/focus representation.

This can provide useful separation between:

```text
Foreground
    ↓
Focused Subject
    ↓
Background
```

This is **relative depth**, not physical distance measurement.

AVCO does not claim that a pixel is a specific number of metres from the camera.

Instead, it attempts to determine relationships between areas of the image.

---

# Depth Consistency

Raw focus analysis can occasionally produce uncertain or missing regions.

For example, a largely uniform area of clothing may contain much less information than the detailed edge surrounding it.

AVCO therefore performs additional reconstruction.

Low-confidence depth samples can borrow information from nearby reliable samples.

Image edges and luminance differences are considered so that the system avoids simply blurring the foreground into the background.

AVCO can also use its own motion vectors to estimate where previous depth information should move.

The depth pipeline therefore becomes:

```text
Focus Analysis
      ↓
Raw Relative Depth
      ↓
Confidence Analysis
      ↓
Edge-Aware Hole Reconstruction
      ↓
Motion-Warped Previous Depth
      ↓
Temporal Consistency
      ↓
Final AVCO Depth Data
```

This is particularly useful for keeping a person represented as a consistent foreground object while they move.

---

# AVCO Reactive Mask

Temporal upscalers need to know when previous image history should no longer be trusted.

AVCO compares the current image against previous motion-compensated information.

Regions that change in ways that cannot be explained by the estimated motion field become increasingly reactive.

This can help identify:

- rapidly moving details
- newly revealed background
- hair movement
- hands and fingers
- changing edges
- other rapidly changing image regions

FSR can then treat historical information in those regions more cautiously.

---

# AVCO Composition Mask

AVCO generates additional composition information using temporal disagreement and motion confidence.

If AVCO cannot confidently determine where an image region came from, the composition mask can indicate that temporal history in that region should be treated more cautiously.

This is particularly useful around:

- complicated boundaries
- rapidly moving objects
- uncertain motion
- changing foreground/background boundaries

---

# AVCO Diagnostic Views

AVCO exposes its generated information through independent diagnostic preview windows.

Available diagnostic views include:

- Motion Vectors
- Depth / Focus Mask
- Reactive Mask
- Composition Mask
- Motion Confidence

Each diagnostic opens in its **own window**.

This means you can simultaneously display, for example:

```text
Main Camera Preview

Motion Vector Preview

Depth Preview

Reactive Mask Preview
```

without replacing the normal camera preview.

The main WebcamUpscaler preview and OBS output continue displaying the processed camera image.

---

# Diagnostic Mask Smoothing

Some raw AVCO masks can change rapidly between frames.

While this is useful data for processing, displaying the raw masks can produce visually uncomfortable flickering.

The diagnostic system therefore supports display-only temporal interpolation.

Intermediate visual states are generated between diagnostic mask updates to make the mask easier to inspect.

Importantly:

**Diagnostic smoothing is not fed into FSR.**

The real AVCO analysis remains unaffected.

```text
AVCO Data
 ├──────────────→ FSR
 │
 └→ Display Smoothing → Debug Window
```

This means making the diagnostic display easier on the eyes does not reduce the quality of the data supplied to the upscaler.

---

# AVCO Tuning

AVCO exposes its processing parameters through live sliders.

Parameters include:

- Analysis resolution
- Motion block size
- Motion search radius
- Motion sensitivity
- Depth sensitivity
- Depth consistency
- Depth history
- Reactive threshold
- Reactive gain
- Composition reactive weighting
- Composition uncertainty weighting
- Diagnostic mask smoothing

These parameters can be adjusted while WebcamUpscaler is running.

AVCO also provides:

**Reset All to Defaults**

to restore the standard configuration.

This allows AVCO to be adapted to different cameras and environments.

For example, a DSLR with strong optical background separation may benefit from different depth-analysis settings than a fixed-focus webcam.

---

# Why AVCO Exists

FSR's normal environment resembles:

```text
3D Game Engine
      ↓
Colour Buffer
Motion Vectors
Scene Information
Temporal Information
Exposure Information
Composition Information
      ↓
FidelityFX
      ↓
Upscaled Image
```

A conventional webcam pipeline is much simpler:

```text
Camera
  ↓
Finished Video Frame
  ↓
Upscaler
```

WebcamUpscaler V2-XB instead attempts:

```text
Camera
  ↓
Raw Camera Frames
  ↓
AVCO
  ├── Motion Estimation
  ├── Relative Depth Estimation
  ├── Temporal Analysis
  ├── Reactive Detection
  ├── Composition Analysis
  └── Confidence Analysis
          ↓
Lightweight AVCO Processor
          ↓
Reconstructed Temporal / Spatial Information
          +
      Camera Image
          ↓
AMD FidelityFX FSR
          ↓
Upscaled Camera Output
```

The goal is not to perfectly recreate a game engine.

The goal is to reconstruct enough useful information that FSR can make better temporal decisions than it could from an ordinary flat video frame alone.

---

# How to Use WebcamUpscaler

## 1. Install WebcamUpscaler

Go to the **Releases** section of this repository.

Download the latest:

```text
WebcamUpscaler Setup x64.exe
```

Run the installer.

You do not need Visual Studio, CMake, Git or the FidelityFX SDK when using a precompiled release.

---

## 2. Connect Your Camera

Connect your camera before starting WebcamUpscaler.

For a normal webcam, make sure Windows can already see the camera.

For Canon EOS cameras using EOS Webcam Utility:

1. Install Canon EOS Webcam Utility.
2. Connect the EOS camera.
3. Turn the camera on.
4. Confirm that EOS Webcam Utility exposes the camera to Windows.
5. Start WebcamUpscaler.

WebcamUpscaler automatically uses its Canon/EOS compatibility behaviour when an appropriate Canon/EOS device is detected.

Other cameras use the generic Media Foundation capture path.

---

## 3. Select Your Camera

Open WebcamUpscaler.

Select the desired camera from the camera list.

For example:

```text
EOS Webcam Utility
```

or:

```text
Logitech HD Pro Webcam C920
```

---

## 4. Select Output Resolution

Choose the output resolution you want WebcamUpscaler to generate.

For example:

```text
1920 × 1080
2560 × 1440
3840 × 2160
```

The output resolution does not need to match the camera's native capture resolution.

FSR performs the upscale between the captured resolution and selected output resolution.

---

## 5. Configure AVCO

Open the AVCO controls.

Individual AVCO systems can be enabled or disabled depending on the camera and desired result.

You can then adjust AVCO's live tuning sliders.

The default configuration is intended to provide a useful starting point.

If experimentation produces a worse result, select:

```text
Reset All to Defaults
```

---

## 6. Inspect AVCO

The AVCO diagnostic tools are available from the application's top menu.

Open:

```text
Debug
  ↓
View
```

You can then open individual windows for:

```text
Motion Vectors
Depth / Focus Mask
Reactive Mask
Composition Mask
Motion Confidence
```

Multiple windows can remain open simultaneously.

These windows are useful when tuning AVCO.

For example, when adjusting depth sensitivity you can leave the Depth / Focus Mask open and immediately see the result.

---

## 7. Start Processing

Start WebcamUpscaler processing.

The main preview will display the processed camera image.

The preview resolution is independent from the full output resolution.

---

# Using WebcamUpscaler With OBS

WebcamUpscaler includes a native OBS source.

You do **not** need to use Window Capture on the WebcamUpscaler preview.

### Setup

1. Install WebcamUpscaler.
2. Start WebcamUpscaler.
3. Select your camera.
4. Select your output resolution.
5. Configure AVCO if required.
6. Start processing.
7. Open OBS Studio.
8. Add a new source.
9. Select:

```text
FSR Camera
```

OBS receives the processed output directly through WebcamUpscaler's output transport.

The pipeline becomes:

```text
Camera
  ↓
WebcamUpscaler
  ↓
AVCO
  ↓
FidelityFX FSR
  ↓
Shared GPU Output
  ↓
FSR Camera OBS Source
  ↓
OBS
```

---

# Building WebcamUpscaler From Source

WebcamUpscaler is designed so that building from source requires very little manual configuration.

## Requirements

You need:

- Windows x64
- Visual Studio with C++ desktop development support
- Windows SDK
- WebcamUpscaler source code
- AMD FidelityFX SDK v1.1.4 ZIP

You do **not** need to manually extract the FidelityFX SDK.

---

## Step 1 — Download the Source

Download the WebcamUpscaler source code from this repository.

You can either clone the repository or use:

```text
Code
↓
Download ZIP
```

Extract the WebcamUpscaler source ZIP somewhere on your computer.

---

## Step 2 — Download FidelityFX SDK v1.1.4

Download:

```text
FidelityFX-SDK-v1.1.4.zip
```

Keep it as a ZIP file.

**Do not manually extract it.**

---

## Step 3 — Place the SDK ZIP Into the Project

Open the WebcamUpscaler source directory.

Find:

```text
gpuobs
└── FSR3_SDK
```

Place:

```text
FidelityFX-SDK-v1.1.4.zip
```

directly inside that folder.

The result should look like:

```text
WebcamUpscaler
│
└── gpuobs
    │
    ├── FSR3_SDK
    │   └── FidelityFX-SDK-v1.1.4.zip
    │
    ├── Prepare-FidelityFX.cmd
    ├── Build-Installer.ps1
    │
    └── ...
```

Do not rename the internal SDK files.

---

## Step 4 — Open the Visual Studio Solution

Inside the source directory, locate the Visual Studio solution file:

```text
.sln
```

Open it in Visual Studio.

If Windows hides file extensions, it may simply appear as a Visual Studio Solution.

---

## Step 5 — Select x64

Make sure Visual Studio is building:

```text
x64
```

rather than Win32/x86.

Use the desired configuration, normally:

```text
Release | x64
```

for a distributable build.

---

## Step 6 — Build

Press:

```text
Ctrl + B
```

or use:

```text
Build
↓
Build Solution
```

The project automatically prepares the FidelityFX SDK.

The build system detects:

```text
FidelityFX-SDK-v1.1.4.zip
```

and automatically extracts/prepares the required SDK files.

You therefore do not need to manually configure FidelityFX include paths every time.

---

# Building the Installer

The project also contains an automated installer build system.

The installer build uses:

```text
Build-Installer.ps1
```

and packages the required WebcamUpscaler components into the Windows installer.

Depending on the solution configuration, the installer target can be built from Visual Studio after the application has successfully compiled.

This means the intended development workflow is approximately:

```text
Download Source
      ↓
Put FidelityFX ZIP in FSR3_SDK
      ↓
Open .sln
      ↓
Ctrl + B
      ↓
Prepare FidelityFX Automatically
      ↓
Compile WebcamUpscaler
      ↓
Build Installer Target
      ↓
WebcamUpscaler Setup x64.exe
```

You should not need to manually copy FidelityFX headers or DLLs into Visual Studio directories.

---

# If FidelityFX Preparation Fails

Check that the ZIP is directly inside:

```text
gpuobs\FSR3_SDK\
```

and not:

```text
gpuobs\FSR3_SDK\FidelityFX-SDK-v1.1.4\FidelityFX-SDK-v1.1.4.zip
```

The expected structure is:

```text
FSR3_SDK
└── FidelityFX-SDK-v1.1.4.zip
```

The build system creates and manages its extracted SDK cache automatically.

If you suspect the extracted SDK cache has become corrupted, delete:

```text
FSR3_SDK\_extracted
```

and build again.

The SDK preparation script will recreate it from the ZIP.

---

# About Frame Generation

The current 2x frame-generation option is experimental camera frame interpolation.

It is **not native AMD FSR Frame Generation**.

AVCO reconstructs several types of temporal and spatial information that raw camera footage does not normally provide.

However, camera capture remains fundamentally different from a rendered game engine.

The interpolation system should therefore not be described as native AMD FSR Frame Generation.

---

# Version Naming

WebcamUpscaler does not use conventional semantic versioning.

Its version numbers are based on **how significantly an update changes the processing pipeline**, rather than simply counting releases.

The general format is:

```text
V<X>[Y].<Z>.<A>
```

## X — Pipeline Generation

The first number represents the fundamental generation of WebcamUpscaler's processing pipeline.

A change to this number means the architecture or processing pipeline has changed significantly enough to be considered a new generation.

Examples:

```text
V1 — Original WebcamUpscaler pipeline

V2 — FidelityFX FSR-based pipeline

V3 — Future major pipeline generation
```

The generation can also advance if the regular update number reaches its rollover point.

---

## Y — Pipeline Modifier

The optional letter after the generation represents a significant modification to the existing pipeline that is important enough to distinguish it from normal updates but does not constitute an entirely new pipeline generation.

If no such modification exists, **there is no letter**.

Pipeline modifiers are ranked from most significant to least significant:

```text
X > Y > Z > B > F
```

For example:

```text
V2X
```

represents a more substantial modification of the V2 pipeline than:

```text
V2B
```

The `X` modifier is associated with the AMD driver/provider override work.

The `B` modifier represents the additional AVCO pipeline changes.

Therefore:

```text
V2-XB
```

represents the V2 FidelityFX architecture with both the X-level provider/override work and B-level AVCO processing modifications.

---

## Z — Regular Update

The first number after the decimal represents normal WebcamUpscaler releases.

This includes regular:

- features
- fixes
- improvements
- UI changes
- compatibility changes

that do not significantly alter the processing pipeline.

Examples:

```text
V2.0
V2.1
V2.2
```

If this number reaches **10**, the version rolls into the next pipeline generation rather than continuing indefinitely.

---

## A — Patch / Small Addition

The final number is used for small patches, additions and minor changes.

Examples:

```text
V2.1.0
V2.1.1
V2.1.2
```

---

## Example Version Progression

```text
V2.0.0
   ↓
V2.1.0
   ↓
V2.1.1
   ↓
V2.2.0
   ↓
V2F.0.0
   ↓
V2B.0.0
   ↓
V2Z.0.0
   ↓
V2Y.0.0
   ↓
V2X
   ↓
V2-XB
   ↓
V3.0.0
```

The version therefore indicates the architectural importance of an update rather than only indicating when it was released.

---

# Why I Made This

WebcamUpscaler was originally created to work around limitations encountered when using Canon EOS Webcam Utility.

I wanted greater control over:

- output resolution
- GPU processing
- FidelityFX upscaling
- frame interpolation
- OBS integration
- temporal processing

Instead of replacing the Canon camera interface, WebcamUpscaler adds an additional GPU processing and analysis stage.

---

# AI-Developed Project

I am not an experienced C++ programmer.

WebcamUpscaler has therefore been developed substantially using OpenAI ChatGPT.

AI has handled a large portion of:

- C++ implementation
- Windows Media Foundation capture
- Direct3D GPU processing
- FidelityFX integration
- AVCO implementation
- GPU texture sharing
- OBS/libobs integration
- CMake and Visual Studio configuration
- compiler and linker debugging
- installer development

The development process has effectively been:

```text
Idea
 ↓
AI Implementation
 ↓
Build
 ↓
Real Hardware Testing
 ↓
Inspect AVCO Output
 ↓
Report Problems
 ↓
Modify
 ↓
Repeat
```

The project is partly an experiment in seeing how far AI-assisted development can allow someone without extensive programming experience to build relatively low-level Windows multimedia software.

---

# Features

- AMD FidelityFX FSR upscaling
- AVCO — Adaptive Variable Capture Optimisations
- Camera-derived motion vectors
- Motion-confidence estimation
- Focus-derived relative depth estimation
- Motion-assisted depth reconstruction
- Reactive-mask generation
- Composition-mask generation
- Live AVCO tuning sliders
- Independent AVCO diagnostic windows
- Diagnostic anti-flicker smoothing
- GPU-accelerated processing
- Canon EOS Webcam Utility support
- Generic Windows Media Foundation camera support
- 4K output
- Experimental 2x frame interpolation
- Native OBS source
- Shared GPU output transport
- No OBS Window Capture required
- Independent preview and output resolution
- Automated FidelityFX SDK preparation
- Automated installer build system
- Windows x64

---

# Open Source

WebcamUpscaler is intended to be an open-source project.

The source code may be studied, modified and redistributed according to the license included with the repository.

Third-party components remain subject to their respective licenses.

In particular, AMD FidelityFX is an AMD technology and is subject to AMD's applicable licensing terms.

The FidelityFX SDK itself should not be assumed to be covered by WebcamUpscaler's own source-code license.

This is why source builds can use a separately downloaded FidelityFX SDK ZIP.

---

# Trademark and Project Identity

**Open-source software licensing and trademark rights are separate.**

Permission to use, modify or redistribute WebcamUpscaler's source code does not automatically grant permission to represent a modified project as an official WebcamUpscaler release.

The following project names and branding are reserved as identifiers of the original project:

- **WebcamUpscaler**
- **AVCO**
- **Adaptive Variable Capture Optimisations**
- official WebcamUpscaler logos
- official AVCO logos
- other official project branding

Unless otherwise permitted by the project owner, modified or independently distributed versions should not use the WebcamUpscaler or AVCO names, logos or branding in a way that implies that the modified version is an official release, is maintained by the original project, or has been endorsed by the original project.

Forks are welcome.

When distributing a substantially modified fork, it is recommended that the fork use its own product name while accurately stating that it is:

```text
Based on WebcamUpscaler
```

or:

```text
Derived from the WebcamUpscaler project
```

where permitted by applicable law and the project's license.

This trademark policy is intended to protect the identity and provenance of official WebcamUpscaler releases, **not to prevent modification of the open-source code**.

The source-code license controls rights to the code.

Trademark law and this project's trademark policy concern the use of project names, logos and other source-identifying branding.

> **Trademark Notice:** WebcamUpscaler™, AVCO™ and Adaptive Variable Capture Optimisations™ are names used to identify the WebcamUpscaler project. All rights in project trademarks and branding are reserved by their respective owner. No trademark license is granted by the project's open-source software license except where explicitly stated.

**Note:** The `™` symbol is an assertion that a name is being used as a trademark; it does not mean that the mark is registered. Do not use the `®` symbol unless the relevant trademark has actually been registered.

---

# Third-Party Trademarks

AMD, AMD FidelityFX and AMD FidelityFX Super Resolution are trademarks or technologies of Advanced Micro Devices, Inc.

Canon and EOS are trademarks of Canon Inc.

Microsoft and Windows are trademarks of Microsoft Corporation.

OBS and OBS Studio are associated with the OBS Project.

OpenAI and ChatGPT are trademarks of OpenAI.

All third-party trademarks belong to their respective owners.

Use of a third-party name in this project is for identification and compatibility purposes and does not imply endorsement, sponsorship or affiliation.

---

# Experimental Software

WebcamUpscaler and AVCO are experimental.

AVCO derives approximate spatial and temporal information from ordinary camera imagery.

Its:

- motion vectors
- relative depth
- reactive information
- composition information
- confidence information

are therefore **estimates**, not ground-truth rendering data.

Hardware, camera, GPU driver and OBS compatibility may vary.

Bugs should be expected.

---

# Disclaimer

WebcamUpscaler and AVCO are independent experimental projects.

WebcamUpscaler is not affiliated with, sponsored by or endorsed by:

- AMD
- Canon
- Microsoft
- OpenAI
- the OBS Project

Canon EOS Webcam Utility is developed by Canon.

OBS Studio is developed by the OBS Project.

AMD FidelityFX and FidelityFX Super Resolution are AMD technologies.

All respective trademarks remain the property of their respective owners.
