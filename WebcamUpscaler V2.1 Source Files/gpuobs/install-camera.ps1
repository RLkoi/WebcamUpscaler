param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$dll=Join-Path $root "x64\$Configuration\VirtualCameraSource.dll"
$reg=Join-Path $root "x64\$Configuration\RegisterVirtualCamera.exe"
if(!(Test-Path $dll)){ throw "Build the solution first; missing $dll" }
if(!(Test-Path $reg)){ throw "Build the solution first; missing $reg" }
& "$env:WINDIR\System32\regsvr32.exe" /s $dll
if($LASTEXITCODE -ne 0){ throw "regsvr32 failed ($LASTEXITCODE)" }
& $reg
if($LASTEXITCODE -ne 0){ throw "MFCreateVirtualCamera failed ($LASTEXITCODE)" }
Write-Host "Registered: FSR3 Upscaled Windows Virtual Camera"
