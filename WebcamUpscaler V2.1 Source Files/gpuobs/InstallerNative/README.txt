This project is the no-WiX WebcamUpscaler V1.0.1 installer.

Final output:
  dist\WebcamUpscaler-V1.0.1-Setup-x64.exe

It embeds WebcamUpscaler.exe, AMD FidelityFX loader/upscaler DLLs, and the native OBS plugin as RCDATA resources and extracts them during installation.
It requests administrator permission because the OBS plugin is installed under C:\ProgramData and uninstall metadata is written to HKLM.

Distribute only the final EXE from dist.
