This project is the no-WiX installer.

The final output is a single native x64 Windows executable:
  dist\FSR-Camera-Setup-x64.exe

It contains the FSR Camera executable and OBS plugin as RCDATA resources and extracts them during installation.
It requires administrator permission because the OBS plugin is installed under C:\ProgramData and uninstall metadata is written to HKLM.

Do not distribute the InstallerNative folder. Distribute only the final EXE from dist.
