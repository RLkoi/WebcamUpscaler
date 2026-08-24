# WebcamUpscaler OBS source

This OBS source consumes the GPU shared texture published by WebcamUpscaler.

## One-click setup on Windows

From the root of the project, right-click `Setup-OBS-Plugin.ps1` and choose **Run with PowerShell**, or run:

```powershell
powershell -ExecutionPolicy Bypass -File .\Setup-OBS-Plugin.ps1
```

The script:

1. clones the official `obsproject/obs-plugintemplate`;
2. uses the template's dependency bootstrap to obtain the matching libobs development files;
3. builds `fsr-camera-source.dll` for x64;
4. installs it to the recommended OBS ProgramData plugin layout:
   `C:\ProgramData\obs-studio\plugins\fsr-camera-source\bin\64bit\fsr-camera-source.dll`;
5. installs the locale data alongside it.

Restart OBS after installation, then choose **Sources -> + -> WebcamUpscaler**.

Use `Uninstall-OBS-Plugin.ps1` to remove it.
