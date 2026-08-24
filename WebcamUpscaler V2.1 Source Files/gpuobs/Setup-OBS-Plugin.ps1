param(
    [string]$Config = "RelWithDebInfo"
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pluginSource = Join-Path $here "OBSPlugin"
$sharedSource = Join-Path $here "Shared"
$work = Join-Path $here ".obs-plugin-build"
$template = Join-Path $work "template"

function Need-Command([string]$name) {
    if (-not (Get-Command $name -ErrorAction SilentlyContinue)) {
        throw "$name was not found in PATH. Install it first (Visual Studio's Git/CMake tools are fine)."
    }
}

Need-Command git
Need-Command cmake

Write-Host "[1/5] Preparing official OBS plugin template..." -ForegroundColor Cyan
if (Test-Path $work) { Remove-Item $work -Recurse -Force }
New-Item -ItemType Directory -Path $work | Out-Null
& git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git $template
if ($LASTEXITCODE -ne 0) { throw "Failed to clone obsproject/obs-plugintemplate." }

Write-Host "[2/5] Injecting FSR Camera plugin source..." -ForegroundColor Cyan
Copy-Item (Join-Path $pluginSource "src\plugin-main.cpp") (Join-Path $template "src\plugin-main.cpp") -Force
Copy-Item (Join-Path $sharedSource "SharedFrame.cpp") (Join-Path $template "src\SharedFrame.cpp") -Force
Copy-Item (Join-Path $sharedSource "SharedFrame.h") (Join-Path $template "src\SharedFrame.h") -Force
New-Item -ItemType Directory -Force -Path (Join-Path $template "data\locale") | Out-Null
Copy-Item (Join-Path $pluginSource "data\locale\en-US.ini") (Join-Path $template "data\locale\en-US.ini") -Force

# Fix the include so the copied source is self-contained in the template.
$main = Join-Path $template "src\plugin-main.cpp"
(Get-Content $main -Raw).Replace('#include "../../Shared/SharedFrame.h"', '#include "SharedFrame.h"') | Set-Content $main -Encoding UTF8

$cmake = @'
cmake_minimum_required(VERSION 3.28...3.30)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES CXX)
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
Set-Content (Join-Path $template "CMakeLists.txt") $cmake -Encoding UTF8

$specPath = Join-Path $template "buildspec.json"
$spec = Get-Content $specPath -Raw | ConvertFrom-Json
$spec.name = "fsr-camera-source"
$spec.displayName = "FSR Camera"
$spec.version = "0.3.0"
$spec.author = "FSR Camera Project"
$spec.website = "https://obsproject.com"
$spec.email = "noreply@example.invalid"
$spec | ConvertTo-Json -Depth 20 | Set-Content $specPath -Encoding UTF8

Write-Host "[3/5] Configuring OBS dependencies and x64 build..." -ForegroundColor Cyan
Push-Location $template
try {
    & cmake --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    Write-Host "[4/5] Building plugin..." -ForegroundColor Cyan
    & cmake --build --preset windows-x64 --config $Config
    if ($LASTEXITCODE -ne 0) { throw "OBS plugin build failed." }
} finally {
    Pop-Location
}

$dll = Get-ChildItem $template -Recurse -Filter "fsr-camera-source.dll" | Where-Object { $_.FullName -match "(RelWithDebInfo|Release)" } | Select-Object -First 1
if (-not $dll) {
    $dll = Get-ChildItem $template -Recurse -Filter "fsr-camera-source.dll" | Select-Object -First 1
}
if (-not $dll) { throw "Build completed but fsr-camera-source.dll could not be located." }

Write-Host "[5/5] Installing into OBS ProgramData plugin directory..." -ForegroundColor Cyan
$destRoot = Join-Path $env:ProgramData "obs-studio\plugins\fsr-camera-source"
$destBin = Join-Path $destRoot "bin\64bit"
$destData = Join-Path $destRoot "data\locale"
New-Item -ItemType Directory -Force -Path $destBin | Out-Null
New-Item -ItemType Directory -Force -Path $destData | Out-Null
Copy-Item $dll.FullName (Join-Path $destBin "fsr-camera-source.dll") -Force
Copy-Item (Join-Path $pluginSource "data\locale\en-US.ini") (Join-Path $destData "en-US.ini") -Force

Write-Host ""
Write-Host "Installed FSR Camera OBS plugin successfully." -ForegroundColor Green
Write-Host "Plugin DLL: $destBin\fsr-camera-source.dll"
Write-Host "Restart OBS, then use Sources -> + -> WebcamUpscaler."
