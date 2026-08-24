@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "OUT=%~1"
if "%OUT%"=="" set "OUT=%ROOT%x64\Release\"
call "%ROOT%Prepare-FidelityFX.cmd"
if errorlevel 1 exit /b %errorlevel%
set "RUNTIME=%ROOT%FSR3_SDK\_extracted\PrebuiltSignedDLL\amd_fidelityfx_dx12.dll"
if not exist "%OUT%" mkdir "%OUT%"
copy /y "%RUNTIME%" "%OUT%amd_fidelityfx_dx12.dll" >nul || exit /b 7
echo FidelityFX v1.1.4 runtime copied to "%OUT%"
exit /b 0
