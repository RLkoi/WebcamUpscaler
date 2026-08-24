param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$root=Split-Path -Parent $MyInvocation.MyCommand.Path
$dll=Join-Path $root "x64\$Configuration\VirtualCameraSource.dll"
$reg=Join-Path $root "x64\$Configuration\RegisterVirtualCamera.exe"
if(Test-Path $reg){ & $reg remove | Out-Null }
if(Test-Path $dll){ & "$env:WINDIR\System32\regsvr32.exe" /s /u $dll }
Write-Host "Virtual camera removed."
