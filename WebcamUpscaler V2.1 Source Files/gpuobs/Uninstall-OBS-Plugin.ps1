$ErrorActionPreference = "Stop"
$dest = Join-Path $env:ProgramData "obs-studio\plugins\fsr-camera-source"
if (Test-Path $dest) {
    Remove-Item $dest -Recurse -Force
    Write-Host "FSR Camera OBS plugin removed. Restart OBS."
} else {
    Write-Host "FSR Camera OBS plugin is not installed in ProgramData."
}
