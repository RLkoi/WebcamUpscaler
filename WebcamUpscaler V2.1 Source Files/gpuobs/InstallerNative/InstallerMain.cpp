#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include "resource.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")

static constexpr wchar_t kProductName[] = L"WebcamUpscaler";
static constexpr wchar_t kVersion[] = L"1.0.1";
static constexpr wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\WebcamUpscaler";
static constexpr wchar_t kPluginRelative[] = L"obs-studio\\plugins\\fsr-camera-source";

static HWND gPathEdit = nullptr;
static HWND gStatus = nullptr;
static HWND gInstallButton = nullptr;

static std::wstring Join(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\') return a + b;
    return a + L"\\" + b;
}

static bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    const size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        const std::wstring parent = path.substr(0, pos);
        if (!parent.empty() && !EnsureDirectory(parent)) return false;
    }
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value)) && value) {
        result = value;
        CoTaskMemFree(value);
    }
    return result;
}

static std::wstring SelfPath() {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    return std::wstring(buf.data(), n);
}

static bool WriteResourceToFile(int id, const std::wstring& destination) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return false;
    HGLOBAL loaded = LoadResource(nullptr, res);
    if (!loaded) return false;
    const DWORD size = SizeofResource(nullptr, res);
    const void* data = LockResource(loaded);
    if (!data || !size) return false;

    const size_t slash = destination.find_last_of(L"\\/");
    if (slash != std::wstring::npos && !EnsureDirectory(destination.substr(0, slash))) return false;

    HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    CloseHandle(file);
    return ok && written == size;
}

static bool CreateShortcut(const std::wstring& target, const std::wstring& shortcutPath, const std::wstring& workingDir) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr)) return false;
    link->SetPath(target.c_str());
    link->SetWorkingDirectory(workingDir.c_str());
    link->SetDescription(L"WebcamUpscaler - AMD FSR 3 camera upscaler and OBS source");
    link->SetIconLocation(target.c_str(), 0);

    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_PPV_ARGS(&persist));
    if (SUCCEEDED(hr)) {
        const size_t slash = shortcutPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) EnsureDirectory(shortcutPath.substr(0, slash));
        hr = persist->Save(shortcutPath.c_str(), TRUE);
        persist->Release();
    }
    link->Release();
    return SUCCEEDED(hr);
}

static void SetRegString(HKEY key, const wchar_t* name, const std::wstring& value) {
    RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

static bool RegisterUninstall(const std::wstring& installDir) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, nullptr, 0,
                        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;

    const std::wstring app = Join(installDir, L"WebcamUpscaler.exe");
    const std::wstring uninstaller = Join(installDir, L"Uninstall.exe");
    SetRegString(key, L"DisplayName", kProductName);
    SetRegString(key, L"DisplayVersion", kVersion);
    SetRegString(key, L"Publisher", L"WebcamUpscaler Project");
    SetRegString(key, L"InstallLocation", installDir);
    SetRegString(key, L"DisplayIcon", app);
    SetRegString(key, L"UninstallString", L"\"" + uninstaller + L"\" --uninstall");
    DWORD one = 1;
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
    RegCloseKey(key);
    return true;
}

static std::wstring ShortcutPath() {
    return Join(Join(KnownFolder(FOLDERID_CommonPrograms), L"WebcamUpscaler"), L"WebcamUpscaler.lnk");
}

static std::wstring PluginRoot() {
    return Join(KnownFolder(FOLDERID_ProgramData), kPluginRelative);
}

static bool InstallPayload(const std::wstring& installDir, std::wstring& error) {
    if (!EnsureDirectory(installDir)) { error = L"Could not create the install directory."; return false; }

    const std::wstring appPath = Join(installDir, L"WebcamUpscaler.exe");
    const std::wstring pluginBin = Join(Join(PluginRoot(), L"bin"), L"64bit");
    const std::wstring pluginData = Join(Join(PluginRoot(), L"data"), L"locale");
    const std::wstring pluginDll = Join(pluginBin, L"fsr-camera-source.dll");
    const std::wstring localeIni = Join(pluginData, L"en-US.ini");

    if (!WriteResourceToFile(IDR_APP_EXE, appPath)) { error = L"Could not install WebcamUpscaler.exe."; return false; }
    if (!WriteResourceToFile(IDR_FFX_RUNTIME, Join(installDir, L"amd_fidelityfx_dx12.dll"))) { error = L"Could not install AMD FidelityFX v1.1.4 DX12 runtime."; return false; }
    if (!WriteResourceToFile(IDR_PLUGIN_DLL, pluginDll)) { error = L"Could not install the OBS plugin DLL. Make sure OBS is closed."; return false; }
    if (!WriteResourceToFile(IDR_LOCALE_INI, localeIni)) { error = L"Could not install the OBS plugin locale file."; return false; }

    const std::wstring self = SelfPath();
    const std::wstring uninstallPath = Join(installDir, L"Uninstall.exe");
    if (!CopyFileW(self.c_str(), uninstallPath.c_str(), FALSE)) { error = L"Could not install the uninstaller."; return false; }

    if (!CreateShortcut(appPath, ShortcutPath(), installDir)) { error = L"Installed, but the Start Menu shortcut could not be created."; }
    if (!RegisterUninstall(installDir)) { error = L"Installed, but Windows Apps & Features registration failed."; }
    return true;
}

static void DeleteTree(const std::wstring& path) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(Join(path, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            const std::wstring child = Join(path, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(child);
            else { SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(child.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(path.c_str());
}

static int RunUninstall() {
    if (MessageBoxW(nullptr, L"Remove WebcamUpscaler and its OBS plugin?", L"WebcamUpscaler Uninstall",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;

    wchar_t module[MAX_PATH * 4]{};
    GetModuleFileNameW(nullptr, module, _countof(module));
    std::wstring self = module;
    const size_t slash = self.find_last_of(L"\\/");
    const std::wstring installDir = slash == std::wstring::npos ? L"" : self.substr(0, slash);

    DeleteFileW(Join(installDir, L"WebcamUpscaler.exe").c_str());
    DeleteFileW(Join(installDir, L"amd_fidelityfx_dx12.dll").c_str());
    DeleteTree(PluginRoot());
    DeleteFileW(ShortcutPath().c_str());
    const std::wstring shortcutDir = Join(KnownFolder(FOLDERID_CommonPrograms), L"WebcamUpscaler");
    RemoveDirectoryW(shortcutDir.c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, kUninstallKey);

    MoveFileExW(self.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(installDir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MessageBoxW(nullptr, L"WebcamUpscaler has been uninstalled.", L"WebcamUpscaler", MB_OK | MB_ICONINFORMATION);
    return 0;
}

static std::wstring BrowseForFolder(HWND owner) {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return L"";
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dialog->SetTitle(L"Choose where to install WebcamUpscaler");
    std::wstring result;
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = Join(path, L"WebcamUpscaler");
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == 1002) {
            const std::wstring folder = BrowseForFolder(hwnd);
            if (!folder.empty()) SetWindowTextW(gPathEdit, folder.c_str());
            return 0;
        }
        if (id == 1003) {
            wchar_t path[32768]{};
            GetWindowTextW(gPathEdit, path, _countof(path));
            if (!*path) { MessageBoxW(hwnd, L"Choose an install folder first.", kProductName, MB_OK | MB_ICONWARNING); return 0; }
            EnableWindow(gInstallButton, FALSE);
            SetWindowTextW(gStatus, L"Installing WebcamUpscaler, AMD FSR 3 runtime and OBS plugin...");
            UpdateWindow(hwnd);
            std::wstring error;
            const bool ok = InstallPayload(path, error);
            if (ok) {
                SetWindowTextW(gStatus, L"Installed successfully. Restart OBS if it is open.");
                MessageBoxW(hwnd, L"WebcamUpscaler is installed. In OBS, add the 'WebcamUpscaler' source.", kProductName, MB_OK | MB_ICONINFORMATION);
                DestroyWindow(hwnd);
            } else {
                SetWindowTextW(gStatus, L"Installation failed.");
                MessageBoxW(hwnd, error.c_str(), L"WebcamUpscaler Setup", MB_OK | MB_ICONERROR);
                EnableWindow(gInstallButton, TRUE);
            }
            return 0;
        }
        if (id == 1004) { DestroyWindow(hwnd); return 0; }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring args = cmdLine ? cmdLine : L"";
    if (args.find(L"--uninstall") != std::wstring::npos) {
        const int rc = RunUninstall();
        CoUninitialize();
        return rc;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"FSRCameraInstallerWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"WebcamUpscaler Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                CW_USEDEFAULT, CW_USEDEFAULT, 560, 280, nullptr, nullptr, instance, nullptr);
    if (!hwnd) { CoUninitialize(); return 1; }

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return c;
    };

    make(L"STATIC", L"WebcamUpscaler", SS_LEFT, 24, 20, 500, 28, 0);
    make(L"STATIC", L"Installs the real AMD FSR 3 camera upscaler and native OBS source.", SS_LEFT, 24, 52, 500, 38, 0);
    make(L"STATIC", L"Install location:", SS_LEFT, 24, 98, 180, 20, 0);

    const std::wstring defaultDir = Join(KnownFolder(FOLDERID_ProgramFiles), L"WebcamUpscaler");
    gPathEdit = make(L"EDIT", defaultDir.c_str(), ES_AUTOHSCROLL | WS_BORDER, 24, 121, 405, 25, 1001);
    make(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 438, 120, 88, 27, 1002);
    gStatus = make(L"STATIC", L"OBS plugin will be installed automatically for all users.", SS_LEFT, 24, 160, 500, 22, 1005);
    gInstallButton = make(L"BUTTON", L"Install", BS_DEFPUSHBUTTON, 340, 204, 88, 30, 1003);
    make(L"BUTTON", L"Cancel", BS_PUSHBUTTON, 438, 204, 88, 30, 1004);

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
