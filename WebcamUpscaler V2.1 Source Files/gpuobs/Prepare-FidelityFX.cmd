@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "SDKDIR=%ROOT%FSR3_SDK"
set "CACHE=%SDKDIR%\_extracted"
set "HEADER=%CACHE%\ffx-api\include\ffx_api\ffx_upscale.h"
set "RUNTIME=%CACHE%\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll"

if not exist "%SDKDIR%" mkdir "%SDKDIR%"

if exist "%HEADER%" if exist "%RUNTIME%" (
  echo FidelityFX SDK v1.1.4 cache ready.
  exit /b 0
)

set "SDKZIP="
for %%F in ("%SDKDIR%\FidelityFX-SDK-v1.1.4.zip") do if exist "%%~fF" set "SDKZIP=%%~fF"
if not defined SDKZIP (
  for %%F in ("%SDKDIR%\FidelityFX-SDK*.zip") do if exist "%%~fF" if not defined SDKZIP set "SDKZIP=%%~fF"
)

if not defined SDKZIP (
  echo ERROR: FidelityFX SDK ZIP not found.
  echo Put FidelityFX-SDK-v1.1.4.zip directly inside:
  echo   "%SDKDIR%"
  exit /b 2
)

echo Extracting FidelityFX SDK from:
echo   "%SDKZIP%"
if exist "%CACHE%" rmdir /s /q "%CACHE%"
mkdir "%CACHE%" || exit /b 3
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%SDKZIP%' -DestinationPath '%CACHE%' -Force"
if errorlevel 1 exit /b 4

if not exist "%HEADER%" (
  echo ERROR: ffx_upscale.h was not found after extraction.
  exit /b 5
)
if not exist "%RUNTIME%" (
  echo ERROR: amd_fidelityfx_dx12.dll was not found after extraction.
  exit /b 6
)

echo FidelityFX SDK v1.1.4 prepared successfully.
exit /b 0
