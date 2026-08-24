#include <windows.h>
#include <algorithm>
#include <commctrl.h>
#include <mfapi.h>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <chrono>
#include <memory>
#include "CameraCapture.h"
#include "Fsr3Upscaler.h"
#include "../Shared/SharedFrame.h"

#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"mfplat.lib")
#pragma comment(lib,"mf.lib")
#pragma comment(lib,"mfreadwrite.lib")
#pragma comment(lib,"mfuuid.lib")

static HWND gInput = nullptr;
static HWND gResolution = nullptr;
static HWND gStart = nullptr;
static HWND gFrameGen = nullptr;
static HWND gStatus = nullptr;
static HWND gStats = nullptr;
static HWND gFsrDiag = nullptr;
static HWND gPreview = nullptr;
static HINSTANCE gInstance = nullptr;

static std::vector<CameraInfo> gCameras;
static std::atomic_bool gRunning = false;
static std::thread gThread;

struct DisplayFrame {
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t sequence = 0;
};

static std::mutex gFrameMutex;
static std::shared_ptr<DisplayFrame> gLatestFrame;
static std::atomic_uint64_t gInputFrames = 0;
static std::atomic_uint64_t gOutputFrames = 0;
static std::atomic_uint64_t gGeneratedFrames = 0;
static std::atomic_uint64_t gFsrDispatches = 0;
static std::atomic_uint32_t gFsrLastCode = 0xFFFFFFFFu;
static std::atomic_bool gFsrContextReady = false;
static std::atomic_bool gFsrActive = false;
static std::atomic_uint32_t gFsrInputW = 0, gFsrInputH = 0, gFsrOutputW = 0, gFsrOutputH = 0;
static std::mutex gFsrDiagMutex;
static std::wstring gFsrProvider = L"Not initialized";

constexpr UINT WM_VIDEO_FRAME = WM_APP + 1;
constexpr UINT WM_STATS_TICK = WM_APP + 2;

static void SetStatus(const std::wstring& text) {
    if (gStatus) SetWindowTextW(gStatus, text.c_str());
}

static void ParseResolution(std::wstring text, uint32_t& w, uint32_t& h) {
    w = 1920;
    h = 1080;
    const auto p = text.find(L'x');
    if (p != std::wstring::npos) {
        w = static_cast<uint32_t>(_wtoi(text.substr(0, p).c_str()));
        h = static_cast<uint32_t>(_wtoi(text.substr(p + 1).c_str()));
    }
}

static void PublishFrame(const std::vector<uint8_t>& frame, uint32_t w, uint32_t h) {
    auto next = std::make_shared<DisplayFrame>();
    next->pixels = frame;
    next->width = w;
    next->height = h;
    next->sequence = gInputFrames.load();

    {
        std::lock_guard<std::mutex> lock(gFrameMutex);
        gLatestFrame = std::move(next);
    }

    if (gPreview) PostMessageW(gPreview, WM_VIDEO_FRAME, 0, 0);
}

static std::shared_ptr<DisplayFrame> GetLatestFrame() {
    std::lock_guard<std::mutex> lock(gFrameMutex);
    return gLatestFrame;
}

static void PaintVideoWindow(HWND hwnd, HDC dc, bool fullResolution) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int clientW = rc.right - rc.left;
    const int clientH = rc.bottom - rc.top;

    auto frame = GetLatestFrame();
    if (!frame || frame->pixels.empty() || !frame->width || !frame->height || clientW <= 0 || clientH <= 0) {
        FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(220,220,220));
        const wchar_t* text = fullResolution ? L"Waiting for full-resolution output..." : L"Waiting for processed frames...";
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    int drawX = 0, drawY = 0, drawW = clientW, drawH = clientH;
    if (!fullResolution) {
        const double srcAspect = static_cast<double>(frame->width) / frame->height;
        const double dstAspect = static_cast<double>(clientW) / clientH;
        if (dstAspect > srcAspect) {
            drawW = static_cast<int>(clientH * srcAspect);
            drawX = (clientW - drawW) / 2;
        } else {
            drawH = static_cast<int>(clientW / srcAspect);
            drawY = (clientH - drawH) / 2;
        }
    }

    // Paint into a memory DC first. OBS and the on-screen preview only ever see
    // a complete finished frame, which avoids the clear/draw flicker of the old path.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, clientW, clientH);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT memRc{0,0,clientW,clientH};
    FillRect(mem, &memRc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = static_cast<LONG>(frame->width);
    bmi.bmiHeader.biHeight = -static_cast<LONG>(frame->height);
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(mem, HALFTONE);
    StretchDIBits(mem,
                  drawX, drawY, drawW, drawH,
                  0, 0, static_cast<int>(frame->width), static_cast<int>(frame->height),
                  frame->pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

    BitBlt(dc, 0, 0, clientW, clientH, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

static LRESULT CALLBACK VideoWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    const bool fullResolution = false;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_VIDEO_FRAME:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        PaintVideoWindow(hwnd, dc, fullResolution);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        // Keep the output surface alive while processing; hiding it would make
        // Window Capture unreliable. Stop processing from the main window instead.
        if (gRunning) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

static void ResizeClient(HWND hwnd, uint32_t width, uint32_t height) {
    RECT rc{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void ShowOutputWindows(uint32_t outW, uint32_t outH) {
    // Human preview only. OBS receives the full-resolution shared frame through
    // the FSR Camera Source plugin, so the size of this window is irrelevant.
    (void)outW; (void)outH;
    ResizeClient(gPreview, 960, 540);
    ShowWindow(gPreview, SW_SHOWNOACTIVATE);
    InvalidateRect(gPreview, nullptr, FALSE);
}

static void Worker(int cameraIndex, uint32_t outW, uint32_t outH, bool frameGen) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    CameraCapture cap;
    std::wstring err;
    fvc::SharedGpuFrameWriter gpuWriter;
    Fsr3Upscaler scaler;

    if (cameraIndex < 0 || cameraIndex >= static_cast<int>(gCameras.size()) ||
        !cap.Open(gCameras[cameraIndex], err)) {
        SetStatus(L"Input error: " + err);
        gRunning = false;
        goto done;
    }

    if (!gpuWriter.Open()) {
        SetStatus(L"Could not create GPU transport for OBS.");
        gRunning = false;
        goto done;
    }

    {
        uint64_t frameNo = 0;
        bool initialized = false;
        uint32_t lastW = 0, lastH = 0;
        auto lastArrival = std::chrono::steady_clock::now();
        double estimatedInputMs = 33.333;

        while (gRunning) {
            std::vector<uint8_t> in;
            uint32_t w = 0, h = 0, stride = 0;

            if (!cap.ReadFrame(in, w, h, stride, err)) {
                Sleep(1);
                continue;
            }
            ++gInputFrames;

            // The on-screen window is intentionally only a lightweight camera
            // preview. OBS receives the full selected output resolution through
            // the shared GPU texture below.
            PublishFrame(in, w, h);

            const auto now = std::chrono::steady_clock::now();
            const double deltaMs = std::chrono::duration<double, std::milli>(now - lastArrival).count();
            lastArrival = now;
            if (deltaMs > 4.0 && deltaMs < 250.0)
                estimatedInputMs = estimatedInputMs * 0.85 + deltaMs * 0.15;

            if (!initialized || w != lastW || h != lastH) {
                scaler.Shutdown();
                if (!scaler.Initialize(w, h, outW, outH, err)) {
                    SetStatus(L"GPU initialization error: " + err);
                    break;
                }
                initialized = true;
                lastW = w;
                lastH = h;
                gFsrContextReady = scaler.UsingFidelityFX();
                gFsrActive = false;
                gFsrDispatches = 0;
                gFsrLastCode = 0xFFFFFFFFu;
                gFsrInputW = w; gFsrInputH = h; gFsrOutputW = outW; gFsrOutputH = outH;
                {
                    std::lock_guard<std::mutex> lock(gFsrDiagMutex);
                    gFsrProvider = scaler.FidelityFXVersion().empty() ? L"AMD FSR 3 provider" : scaler.FidelityFXVersion();
                }
                const std::wstring fsrName = scaler.FidelityFXVersion().empty() ? L"AMD FSR 3" : scaler.FidelityFXVersion();
                SetStatus(frameGen
                    ? (L"Real " + fsrName + L" upscaling active. OBS source: WebcamUpscaler. Experimental 2x interpolation enabled.")
                    : (L"Real " + fsrName + L" upscaling active. OBS source: WebcamUpscaler."));
            }

            if (!scaler.UpscaleBGRA(in.data(), stride, err)) {
                gFsrActive = false;
                gFsrLastCode = scaler.LastDispatchCode();
                SetStatus(L"GPU upscale failed: " + err);
                break;
            }
            // ACTIVE is only set after the real FidelityFX ffxDispatch call returned FFX_API_RETURN_OK.
            gFsrActive = scaler.LastDispatchSucceeded();
            gFsrLastCode = scaler.LastDispatchCode();
            gFsrDispatches = scaler.SuccessfulDispatchCount();

            if (frameGen && scaler.HasPrevious()) {
                uint64_t handle = 0;
                if (scaler.PublishMidpoint(handle, err)) {
                    gpuWriter.Publish(handle, outW, outH, frameNo++, 60);
                    ++gOutputFrames;
                    ++gGeneratedFrames;

                    // Place the synthetic midpoint roughly halfway between real
                    // camera frames. The interpolation itself is GPU-only.
                    const DWORD halfMs = static_cast<DWORD>(std::clamp(estimatedInputMs * 0.5, 1.0, 20.0));
                    Sleep(halfMs);
                }
            }

            uint64_t handle = 0;
            if (!scaler.PublishCurrent(handle, err)) {
                SetStatus(L"GPU publish failed: " + err);
                break;
            }
            gpuWriter.Publish(handle, outW, outH, frameNo++, frameGen ? 60 : 30);
            ++gOutputFrames;
            scaler.CommitCurrentAsPrevious();
        }
    }

    scaler.Shutdown();
    cap.Close();
    gpuWriter.Close();

done:
    MFShutdown();
    CoUninitialize();
}
static void StartStop() {
    if (gRunning) {
        gRunning = false;
        if (gThread.joinable()) gThread.join();
        SetWindowTextW(gStart, L"Start");
        SetStatus(L"Stopped");
        return;
    }

    const int idx = static_cast<int>(SendMessageW(gInput, CB_GETCURSEL, 0, 0));
    wchar_t res[64]{};
    GetWindowTextW(gResolution, res, 64);
    uint32_t w = 0, h = 0;
    ParseResolution(res, w, h);
    const bool frameGen = SendMessageW(gFrameGen, BM_GETCHECK, 0, 0) == BST_CHECKED;

    if (idx < 0) {
        MessageBoxW(nullptr, L"Select an input camera first.", L"WebcamUpscaler", MB_ICONWARNING);
        return;
    }
    if (w > fvc::kMaxWidth || h > fvc::kMaxHeight || w < 64 || h < 64) {
        MessageBoxW(nullptr, L"Resolution must be between 64x64 and 3840x2160.", L"WebcamUpscaler", MB_ICONWARNING);
        return;
    }

    gInputFrames = gOutputFrames = gGeneratedFrames = 0;
    gFsrDispatches = 0;
    gFsrLastCode = 0xFFFFFFFFu;
    gFsrContextReady = false;
    gFsrActive = false;
    gFsrInputW = gFsrInputH = gFsrOutputW = gFsrOutputH = 0;
    { std::lock_guard<std::mutex> lock(gFsrDiagMutex); gFsrProvider = L"Not initialized"; }
    {
        std::lock_guard<std::mutex> lock(gFrameMutex);
        gLatestFrame.reset();
    }

    ShowOutputWindows(w, h);
    gRunning = true;
    SetWindowTextW(gStart, L"Stop");
    if (frameGen)
        SetStatus(L"Running. In OBS add Source -> WebcamUpscaler. Experimental 2x interpolation enabled.");
    else
        SetStatus(L"Running. In OBS add Source -> WebcamUpscaler.");
    gThread = std::thread(Worker, idx, w, h, frameGen);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static uint64_t lastIn = 0, lastOut = 0, lastGen = 0;
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Input", WS_CHILD | WS_VISIBLE,
            20, 22, 120, 22, hwnd, nullptr, nullptr, nullptr);
        gInput = CreateWindowW(WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            150, 18, 430, 220, hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);

        CreateWindowW(L"STATIC", L"Output resolution", WS_CHILD | WS_VISIBLE,
            20, 67, 120, 22, hwnd, nullptr, nullptr, nullptr);
        gResolution = CreateWindowW(WC_COMBOBOXW, L"",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | WS_VSCROLL,
            150, 63, 180, 160, hwnd, reinterpret_cast<HMENU>(102), nullptr, nullptr);
        const wchar_t* resolutions[] = { L"1280x720", L"1920x1080", L"2560x1440", L"3840x2160" };
        for (const auto* r : resolutions) SendMessageW(gResolution, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(r));
        SendMessageW(gResolution, CB_SETCURSEL, 1, 0);

        gFrameGen = CreateWindowW(L"BUTTON", L"Experimental GPU 2x frame interpolation",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            350, 64, 250, 24, hwnd, reinterpret_cast<HMENU>(104), nullptr, nullptr);

        gStart = CreateWindowW(L"BUTTON", L"Start",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            20, 110, 130, 34, hwnd, reinterpret_cast<HMENU>(103), nullptr, nullptr);

        gStatus = CreateWindowW(L"STATIC",
            L"Ready. GPU scaling + GPU texture sharing to OBS.",
            WS_CHILD | WS_VISIBLE,
            20, 160, 590, 45, hwnd, nullptr, nullptr, nullptr);
        gStats = CreateWindowW(L"STATIC", L"Input 0 fps | Output 0 fps | Generated 0 fps",
            WS_CHILD | WS_VISIBLE,
            20, 210, 590, 24, hwnd, nullptr, nullptr, nullptr);
        gFsrDiag = CreateWindowW(L"STATIC",
            L"AMD FidelityFX FSR 3: NOT INITIALIZED\r\nSDK: FidelityFX v1.1.4 | Dispatches: 0 | Last: N/A",
            WS_CHILD | WS_VISIBLE,
            20, 240, 590, 64, hwnd, nullptr, nullptr, nullptr);

        gCameras = CameraCapture::Enumerate();
        for (auto& c : gCameras) SendMessageW(gInput, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.name.c_str()));
        if (!gCameras.empty()) SendMessageW(gInput, CB_SETCURSEL, 0, 0);
        else SetStatus(L"No Media Foundation cameras found.");

        SetTimer(hwnd, 1, 1000, nullptr);
        return 0;
    }

    case WM_TIMER: {
        const uint64_t in = gInputFrames.load();
        const uint64_t out = gOutputFrames.load();
        const uint64_t gen = gGeneratedFrames.load();
        wchar_t text[160]{};
        swprintf_s(text, L"Input %llu fps | Output %llu fps | Generated %llu fps",
                   static_cast<unsigned long long>(in - lastIn),
                   static_cast<unsigned long long>(out - lastOut),
                   static_cast<unsigned long long>(gen - lastGen));
        lastIn = in; lastOut = out; lastGen = gen;
        if (gStats) SetWindowTextW(gStats, text);

        if (gFsrDiag) {
            std::wstring provider;
            { std::lock_guard<std::mutex> lock(gFsrDiagMutex); provider = gFsrProvider; }
            const bool contextReady = gFsrContextReady.load();
            const bool active = gFsrActive.load();
            const uint32_t code = gFsrLastCode.load();
            const uint64_t dispatches = gFsrDispatches.load();
            wchar_t diag[640]{};
            const wchar_t* state = active ? L"ACTIVE" : (contextReady ? L"READY - WAITING FOR SUCCESSFUL DISPATCH" : L"NOT INITIALIZED");
            wchar_t result[64]{};
            if (code == 0xFFFFFFFFu) wcscpy_s(result, L"N/A");
            else if (code == static_cast<uint32_t>(FFX_API_RETURN_OK)) wcscpy_s(result, L"FFX_API_RETURN_OK");
            else swprintf_s(result, L"FidelityFX code %u", code);
            swprintf_s(diag,
                L"AMD FidelityFX FSR 3: %s\r\nSDK: FidelityFX v1.1.4 | Provider: %s\r\nDispatches: %llu | Last: %s | %ux%u -> %ux%u",
                state, provider.c_str(), static_cast<unsigned long long>(dispatches), result,
                gFsrInputW.load(), gFsrInputH.load(), gFsrOutputW.load(), gFsrOutputH.load());
            SetWindowTextW(gFsrDiag, diag);
        }
        return 0;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 103) StartStop();
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        gRunning = false;
        if (gThread.joinable()) gThread.join();
        if (gPreview) DestroyWindow(gPreview);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    gInstance = instance;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION);

    INITCOMMONCONTROLSEX ic{sizeof(ic), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&ic);

    WNDCLASSW mainClass{};
    mainClass.lpfnWndProc = MainWndProc;
    mainClass.hInstance = instance;
    mainClass.lpszClassName = L"FsrCameraProcessorWnd";
    mainClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    mainClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&mainClass);

    WNDCLASSW videoClass{};
    videoClass.lpfnWndProc = VideoWndProc;
    videoClass.hInstance = instance;
    videoClass.lpszClassName = L"FsrVideoSurfaceWnd";
    videoClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    videoClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&videoClass);

    HWND mainWindow = CreateWindowW(mainClass.lpszClassName, L"WebcamUpscaler",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 390, nullptr, nullptr, instance, nullptr);

    gPreview = CreateWindowW(videoClass.lpszClassName, L"FSR Preview",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 540,
        nullptr, nullptr, instance, nullptr);

    ShowWindow(mainWindow, show);
    UpdateWindow(mainWindow);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    MFShutdown();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
