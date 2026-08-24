param(
    [ValidateSet("Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$pluginWork = Join-Path $root ".obs-plugin-build"
$template = Join-Path $pluginWork "template"
$payload = Join-Path $root "InstallerNative\payload"
$dist = Join-Path $root "dist"

function Need-Command([string]$name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if (-not $cmd) { throw "$name was not found. Run Build-Installer.cmd from a Visual Studio Developer Terminal." }
    return $cmd.Source
}

$cmake = Need-Command "cmake"
$git = Need-Command "git"
$msbuild = Need-Command "msbuild"

Write-Host ""; Write-Host "FSR Camera native installer builder (no WiX)" -ForegroundColor Cyan
Write-Host "================================================" -ForegroundColor Cyan

# Keep the clamp compile fix in the actual camera application source.
$mainCpp = Join-Path $root "Fsr3VirtualCamera\main.cpp"
$mainText = Get-Content $mainCpp -Raw
if ($mainText -notmatch '#include\s*<algorithm>') {
    $mainText = $mainText -replace '#include <windows.h>', "#include <windows.h>`r`n#include <algorithm>"
    Set-Content $mainCpp $mainText -Encoding UTF8
}

# VS 2026 is strict about designated initializer ordering.
$pluginSource = Join-Path $root "OBSPlugin\src\plugin-main.cpp"
$ptext = Get-Content $pluginSource -Raw
$ptext = $ptext.Replace("    .video_render = fsr_video_render,`r`n    .video_tick = fsr_video_tick,", "    .video_tick = fsr_video_tick,`r`n    .video_render = fsr_video_render,")
$ptext = $ptext.Replace("    .video_render = fsr_video_render,`n    .video_tick = fsr_video_tick,", "    .video_tick = fsr_video_tick,`n    .video_render = fsr_video_render,")
Set-Content $pluginSource $ptext -Encoding UTF8

Write-Host ""; Write-Host "[1/5] Building FSR Camera Release x64..." -ForegroundColor Cyan
$appProject = Join-Path $root "Fsr3VirtualCamera\Fsr3VirtualCamera.vcxproj"
$solutionDir = $root + "\"
& $msbuild $appProject /m /t:Build /p:Configuration=$Configuration /p:Platform=x64 "/p:SolutionDir=$solutionDir"
if ($LASTEXITCODE -ne 0) { throw "FSR Camera build failed." }
$appExe = Join-Path $root "x64\$Configuration\Fsr3VirtualCamera.exe"
if (-not (Test-Path $appExe)) { throw "Fsr3VirtualCamera.exe was not produced at $appExe" }

Write-Host ""; Write-Host "[2/5] Preparing OBS plugin build..." -ForegroundColor Cyan
if (Test-Path $pluginWork) { Remove-Item $pluginWork -Recurse -Force }
New-Item -ItemType Directory -Force -Path $pluginWork | Out-Null
& $git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git $template
if ($LASTEXITCODE -ne 0) { throw "Could not clone the official OBS plugin template." }

Copy-Item (Join-Path $root "OBSPlugin\src\plugin-main.cpp") (Join-Path $template "src\plugin-main.cpp") -Force
Copy-Item (Join-Path $root "Shared\SharedFrame.cpp") (Join-Path $template "src\SharedFrame.cpp") -Force
Copy-Item (Join-Path $root "Shared\SharedFrame.h") (Join-Path $template "src\SharedFrame.h") -Force
New-Item -ItemType Directory -Force -Path (Join-Path $template "data\locale") | Out-Null
Copy-Item (Join-Path $root "OBSPlugin\data\locale\en-US.ini") (Join-Path $template "data\locale\en-US.ini") -Force

$pluginMain = Join-Path $template "src\plugin-main.cpp"
(Get-Content $pluginMain -Raw).Replace('#include "../../Shared/SharedFrame.h"', '#include "SharedFrame.h"') | Set-Content $pluginMain -Encoding UTF8

# C is mandatory because the official template generates plugin-support.c.
$cmakeText = @'
cmake_minimum_required(VERSION 3.28)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES C CXX)
include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/plugin-main.cpp
    src/SharedFrame.cpp
    src/SharedFrame.h
)
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
'@
Set-Content (Join-Path $template "CMakeLists.txt") $cmakeText -Encoding UTF8

$specPath = Join-Path $template "buildspec.json"
$spec = Get-Content $specPath -Raw | ConvertFrom-Json
$spec.name = "fsr-camera-source"
$spec.displayName = "FSR Camera"
$spec.version = "0.5.0"
$spec.author = "FSR Camera Project"
$spec.website = "https://obsproject.com"
$spec.email = "noreply@example.invalid"
$spec | ConvertTo-Json -Depth 20 | Set-Content $specPath -Encoding UTF8

Push-Location $template
try {
    Write-Host ""; Write-Host "[3/5] Configuring/building OBS plugin..." -ForegroundColor Cyan
    # Do not use the template's VS 2022 preset. CMake selects the installed VS (including VS 2026).
    & $cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw "OBS plugin CMake configure failed." }
    & $cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw "OBS plugin build failed." }
} finally {
    Pop-Location
}

$pluginDll = Get-ChildItem (Join-Path $template "build") -Recurse -Filter "fsr-camera-source.dll" |
    Where-Object { $_.FullName -match '\\Release\\' -and $_.FullName -notmatch '\\rundir\\' } |
    Select-Object -First 1
if (-not $pluginDll) { $pluginDll = Get-ChildItem (Join-Path $template "build") -Recurse -Filter "fsr-camera-source.dll" | Select-Object -First 1 }
if (-not $pluginDll) { throw "fsr-camera-source.dll was not produced." }

Write-Host ""; Write-Host "[4/5] Embedding app + OBS plugin into installer..." -ForegroundColor Cyan
if (Test-Path $payload) { Remove-Item $payload -Recurse -Force }
New-Item -ItemType Directory -Force -Path $payload | Out-Null
Copy-Item $appExe (Join-Path $payload "Fsr3VirtualCamera.exe") -Force
Copy-Item $pluginDll.FullName (Join-Path $payload "fsr-camera-source.dll") -Force
Copy-Item (Join-Path $root "OBSPlugin\data\locale\en-US.ini") (Join-Path $payload "en-US.ini") -Force

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Write-Host ""; Write-Host "[5/5] Building standalone native installer..." -ForegroundColor Cyan
& $msbuild (Join-Path $root "InstallerNative\InstallerNative.vcxproj") /m /t:Build /p:Configuration=Release /p:Platform=x64
if ($LASTEXITCODE -ne 0) { throw "Native installer build failed." }

$installer = Join-Path $dist "FSR-Camera-Setup-x64.exe"
if (-not (Test-Path $installer)) { throw "Installer executable was not produced at $installer" }

Write-Host ""
Write-Host "SUCCESS" -ForegroundColor Green
Write-Host "Standalone installer: $installer" -ForegroundColor Green
Write-Host ""
Write-Host "The target PC only needs this one EXE."
Write-Host "It installs the app, OBS plugin, locale data, Start Menu shortcut, and an uninstaller."
Write-Host "No WiX, NuGet, CMake, Git, or Visual Studio are needed on the target PC."
