// Native visual smoke harness: use the shipping message handlers and resources,
// but never enter the production startup flow, enumerate user windows, register
// hotkeys, or touch startup registration. Every capture is bounded to an owned
// window before any crops or contact sheets are made.
#include "../src/WindowCueNative.h"
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdio>

namespace windowcue {
StartupStatus visualStartupStatus{StartupState::Enabled, L""};
int visualStartupChanges{};
StartupStatus QueryVisualStartupStatus() { return visualStartupStatus; }
StartupStatus SetVisualStartupEnabled(bool enabled) {
    ++visualStartupChanges;
    visualStartupStatus.state = enabled ? StartupState::Enabled : StartupState::Disabled;
    return visualStartupStatus;
}
}
#define QueryStartupStatus QueryVisualStartupStatus
#define SetStartupEnabled SetVisualStartupEnabled
#define wWinMain WindowCueProductionEntry
#include "../src/Main.cpp"
#undef wWinMain
#undef SetStartupEnabled
#undef QueryStartupStatus

namespace {
using Microsoft::WRL::ComPtr;
struct Pixels {
    int width{}, height{};
    std::vector<BYTE> bgra;
    bool valid() const { return width > 0 && height > 0 && !bgra.empty(); }
};

void Pump(unsigned milliseconds) {
    ULONGLONG until = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) continue;
            if (!windowcue::HandleSettingsMessage(message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (GetTickCount64() >= until) break;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    } while (true);
}

RECT VisibleFrame(HWND window) {
    RECT bounds{};
    GetWindowRect(window, &bounds);
    // DWM frame bounds exclude the invisible resize/shadow padding, where a
    // screen DC would otherwise include pixels belonging to desktop apps.
    RECT visibleBounds{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleBounds, sizeof(visibleBounds))) &&
        visibleBounds.right > visibleBounds.left && visibleBounds.bottom > visibleBounds.top)
        bounds = visibleBounds;
    return bounds;
}

Pixels CaptureOwnedWindowOnce(HWND window, std::ofstream& report) {
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window)) return {};
    // Foreground activation can be refused while the user is working. Keep only
    // the temporary test surface above normal apps, without activating it, so an
    // exact rectangle never captures an unrelated window that occludes ours.
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(window);
    DwmFlush();
    // Let DWM finish the initial visible/z-order transition before validating
    // ownership at the capture points. No message pumping changes sample state.
    Sleep(50);
    RECT bounds = VisibleFrame(window);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return {};
    for (int y : {3, height / 2, height - 4}) {
        for (int x : {3, width / 2, width - 4}) {
            HWND surface = WindowFromPoint({bounds.left + x, bounds.top + y});
            DWORD visibleProcess{};
            GetWindowThreadProcessId(surface, &visibleProcess);
            if (visibleProcess != GetCurrentProcessId()) {
                report << "capture_refused_occluded_owned_surface\n";
                return {};
            }
        }
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    HDC screen = GetDC(nullptr);
    HDC destination = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!screen || !destination || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (destination) DeleteDC(destination);
        if (screen) ReleaseDC(nullptr, screen);
        return {};
    }
    HGDIOBJ previous = SelectObject(destination, bitmap);
    DwmFlush();
    BOOL copied = BitBlt(destination, 0, 0, width, height, screen,
                         bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);
    const char* method = "bounded-screen-DC";
    if (!copied) {
        method = "PrintWindow-fallback";
        copied = PrintWindow(window, destination, 2 /* PW_RENDERFULLCONTENT */);
    }
    Pixels pixels;
    if (copied) {
        pixels = Pixels{width, height, std::vector<BYTE>(static_cast<size_t>(width) * height * 4)};
        std::memcpy(pixels.bgra.data(), bits, pixels.bgra.size());
        // Screen DC alpha is unspecified. These are opaque composed pixels.
        for (size_t offset = 3; offset < pixels.bgra.size(); offset += 4) pixels.bgra[offset] = 255;
        report << "capture " << method << " owned_pid=" << process << " rect="
               << bounds.left << ',' << bounds.top << ',' << bounds.right << ',' << bounds.bottom << '\n';
    }
    SelectObject(destination, previous);
    DeleteObject(bitmap);
    DeleteDC(destination);
    ReleaseDC(nullptr, screen);
    return pixels;
}

Pixels CaptureOwnedWindow(HWND window, std::ofstream& report) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        Pixels result = CaptureOwnedWindowOnce(window, report);
        if (result.valid()) return result;
        // An initial DWM show transition can briefly leave the old foreground
        // surface at a hit-test point. Never capture it; retry the owned window.
        Sleep(50);
    }
    return {};
}

bool WritePng(IWICImagingFactory* factory, const std::filesystem::path& path, const Pixels& pixels) {
    if (!pixels.valid() || std::filesystem::exists(path)) return false;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(pixels.width, pixels.height))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || format != GUID_WICPixelFormat32bppBGRA) return false;
    return SUCCEEDED(frame->WritePixels(pixels.height, pixels.width * 4,
        static_cast<UINT>(pixels.bgra.size()), const_cast<BYTE*>(pixels.bgra.data()))) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit());
}

Pixels Crop(const Pixels& pixels, RECT rect) {
    rect.left = std::clamp(rect.left, 0L, static_cast<LONG>(pixels.width));
    rect.right = std::clamp(rect.right, rect.left, static_cast<LONG>(pixels.width));
    rect.top = std::clamp(rect.top, 0L, static_cast<LONG>(pixels.height));
    rect.bottom = std::clamp(rect.bottom, rect.top, static_cast<LONG>(pixels.height));
    Pixels crop{rect.right - rect.left, rect.bottom - rect.top, {}};
    if (crop.width <= 0 || crop.height <= 0) return {};
    crop.bgra.resize(static_cast<size_t>(crop.width) * crop.height * 4);
    for (int row = 0; row < crop.height; ++row)
        std::memcpy(crop.bgra.data() + static_cast<size_t>(row) * crop.width * 4,
            pixels.bgra.data() + (static_cast<size_t>(row + rect.top) * pixels.width + rect.left) * 4,
            static_cast<size_t>(crop.width) * 4);
    return crop;
}

Pixels Stack(const std::vector<Pixels>& rows) {
    int width{}, height{};
    for (const auto& row : rows) if (row.valid()) { width = std::max(width, row.width); height += row.height + 12; }
    Pixels result{width, height, std::vector<BYTE>(static_cast<size_t>(width) * height * 4, 245)};
    for (size_t i = 3; i < result.bgra.size(); i += 4) result.bgra[i] = 255;
    int top{};
    for (const auto& row : rows) if (row.valid()) {
        for (int y = 0; y < row.height; ++y)
            std::memcpy(result.bgra.data() + (static_cast<size_t>(top + y) * width) * 4,
                row.bgra.data() + static_cast<size_t>(y) * row.width * 4, static_cast<size_t>(row.width) * 4);
        top += row.height + 12;
    }
    return result;
}

void SetCaptionSample(double level, bool pressed) {
    using namespace windowcue;
    KillTimer(g.hwnd, kCaptionAnimationTimer);
    g.captionAnimationRunning = false;
    g.settingsButtonHovered = level > 0;
    g.settingsButtonPressed = pressed;
    g.settingsHoverAnimation = {level, level, level, GetTickCount64()};
    g.themeButtonHovered = false;
    g.themeButtonPressed = false;
    g.themeHoverAnimation = {};
    InvalidateRect(g.hwnd, nullptr, FALSE);
    UpdateWindow(g.hwnd);
    DwmFlush();
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace windowcue;
    if (argc != 2 || GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", nullptr, 0) < 2) return 90;
    const std::filesystem::path output(argv[1]);
    if (!std::filesystem::is_directory(output) || std::filesystem::exists(output / L"caption-report.txt")) return 91;
    std::ofstream report(output / L"caption-report.txt");
    report << "Actual native UI, production Main.cpp/SVG resources; startup API mocked.\n"
           << "No production entry, user window enumeration, tray registration, settings load or hotkey registration.\n"
           << "Capture source: exact owned-window rectangles only; no full-desktop capture.\n"
           << "96/144/192 DPI are layout simulations, not real mixed-monitor validation.\n";
    HWND originalForeground = GetForegroundWindow();
    POINT originalCursor{};
    GetCursorPos(&originalCursor);
    EnableDpiAwareness();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 92;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        CoUninitialize(); return 93;
    }
    g.instance = GetModuleHandleW(nullptr);
    g.appIcon = static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(IDI_WINDOWCUE), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
    WNDCLASSW wc{};
    wc.hInstance = g.instance;
    wc.lpfnWndProc = WindowProcedure;
    wc.lpszClassName = kWindowClass;
    wc.hIcon = g.appIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    int failure{};
    if (!RegisterClassW(&wc)) failure = 94;
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const int left = monitor.rcWork.left + 20, top = monitor.rcWork.top + 20;
    // resizingForContent keeps the deliberately empty preview at a fixed height.
    g.resizingForContent = true;
    HWND host = failure ? nullptr : CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"WindowCue visual smoke",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        left, top, 1080, 310, nullptr, nullptr, g.instance, nullptr);
    if (!host) failure = 95;
    const UINT actualDpi = g.dpi;
    report << "monitor_dpi=" << actualDpi << '\n';
    auto save = [&](const wchar_t* name, const Pixels& pixels) {
        if (!WritePng(factory.Get(), output / name, pixels) && !failure) failure = 96;
    };
    std::vector<Pixels> captionRows;
    if (host) {
        // No application timers are registered because the normal entry is bypassed.
        ShowWindow(host, SW_SHOWNOACTIVATE);
        SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetForegroundWindow(host);
        POINT neutral{50, 100};
        ClientToScreen(host, &neutral);
        SetCursorPos(neutral.x, neutral.y);
        Pump(150);
        for (bool dark : {false, true}) {
            g.darkMode = dark;
            RefreshThemeIcons();
            ApplyDwmFrame(host);
            UpdateCaptionTooltipText();
            ApplySettingsTheme();
            HideSettingsWindow();
            SetForegroundWindow(host);
            Pump(50);
            SetCaptionSample(0, false);
            Pixels main = CaptureOwnedWindow(host, report);
            save(dark ? L"dark-main.png" : L"light-main.png", main);
            for (double level : {0., .5, 1.}) {
                SetCaptionSample(level, false);
                Pixels state = CaptureOwnedWindow(host, report);
                RECT windowBounds = VisibleFrame(host);
                POINT clientOrigin{};
                ClientToScreen(host, &clientOrigin);
                int x = clientOrigin.x - windowBounds.left;
                int y = clientOrigin.y - windowBounds.top;
                captionRows.push_back(Crop(state, {x + g.settingsButtonRect.left - Scale(8), y,
                    x + g.themeButtonRect.right + Scale(8), y + Scale(34)}));
                report << (dark ? "dark" : "light") << " hover_level=" << level << '\n';
            }
            SetCaptionSample(1, true);
            Pixels pressed = CaptureOwnedWindow(host, report);
            RECT wr = VisibleFrame(host);
            POINT origin{}; ClientToScreen(host, &origin);
            captionRows.push_back(Crop(pressed, {origin.x - wr.left + g.settingsButtonRect.left - Scale(8),
                origin.y - wr.top, origin.x - wr.left + g.themeButtonRect.right + Scale(8),
                origin.y - wr.top + Scale(34)}));
            SetCaptionSample(0, false);
            ShowSettingsWindow();
            Pump(80);
            SetCaptionSample(0, false);
            Pixels settingsOpen = CaptureOwnedWindow(host, report);
            RECT openedBounds = VisibleFrame(host);
            POINT openedOrigin{}; ClientToScreen(host, &openedOrigin);
            captionRows.push_back(Crop(settingsOpen,
                {openedOrigin.x - openedBounds.left + g.settingsButtonRect.left - Scale(8),
                 openedOrigin.y - openedBounds.top,
                 openedOrigin.x - openedBounds.left + g.themeButtonRect.right + Scale(8),
                 openedOrigin.y - openedBounds.top + Scale(34)}));
            report << (dark ? "dark" : "light") << " settings_open=1 hover_level=0\n";
            Pixels settings = CaptureOwnedWindow(g.settingsWindow, report);
            save(dark ? L"dark-settings.png" : L"light-settings.png", settings);
            save(dark ? L"dark-review.png" : L"light-review.png", Stack({main, settings}));
            HideSettingsWindow();
        }
        // Verify the actual registered tooltip labels and tool hit rectangles.
        report << "tooltip_tools=" << SendMessageW(g.captionTooltips, TTM_GETTOOLCOUNT, 0, 0) << '\n';
        for (UINT_PTR id : {kSettingsTooltipId, kThemeTooltipId}) {
            wchar_t label[256]{};
            TOOLINFOW tool{sizeof(tool)};
            tool.hwnd = host; tool.uId = id;
            if (!SendMessageW(g.captionTooltips, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool))) failure = 97;
            tool.lpszText = label;
            SendMessageW(g.captionTooltips, TTM_GETTEXTW, std::size(label), reinterpret_cast<LPARAM>(&tool));
            report << "tooltip_id=" << id << " rect=" << tool.rect.left << ',' << tool.rect.top << ','
                << tool.rect.right << ',' << tool.rect.bottom << " has_label=" << (label[0] != 0) << '\n';
            if (!label[0] && !failure) failure = 97;
        }
        // Natural hover lets the production tooltip subclass and 500ms delay run.
        SetForegroundWindow(host);
        POINT gear{(g.settingsButtonRect.left + g.settingsButtonRect.right) / 2,
            (g.settingsButtonRect.top + g.settingsButtonRect.bottom) / 2};
        ClientToScreen(host, &gear);
        SetCursorPos(neutral.x, neutral.y);
        Pump(50);
        SetCursorPos(gear.x, gear.y);
        Pump(900);
        report << "tooltip_foreground_is_host=" << (GetForegroundWindow() == host)
            << " pointer_is_host=" << (WindowFromPoint(gear) == host)
            << " hit_test=" << SendMessageW(host, WM_NCHITTEST, 0, MAKELPARAM(gear.x, gear.y)) << '\n';
        if (!IsWindowVisible(g.captionTooltips)) {
            // Explicit relay is diagnostic fallback for test runners that do not
            // deliver pointer-generated messages after a foreground handoff.
            POINT clientGear = gear; ScreenToClient(host, &clientGear);
            MSG motion{};
            motion.hwnd = host; motion.message = WM_MOUSEMOVE;
            motion.lParam = MAKELPARAM(clientGear.x, clientGear.y);
            motion.time = GetTickCount(); motion.pt = gear;
            SendMessageW(g.captionTooltips, TTM_RELAYEVENT, 0, reinterpret_cast<LPARAM>(&motion));
            Pump(650);
            report << "tooltip_explicit_relay_used=1\n";
        }
        if (IsWindowVisible(g.captionTooltips)) {
            save(L"settings-tooltip.png", CaptureOwnedWindow(g.captionTooltips, report));
            report << "natural_tooltip_visible=1\n";
        } else { report << "natural_tooltip_visible=0\n"; failure = 98; }
        PopCaptionTooltip();
        // Its natural behavior is already verified. Keep it out of the isolated
        // DPI comparison so stale pointer tracking does not overlay title text.
        SendMessageW(g.captionTooltips, TTM_ACTIVATE, FALSE, 0);
        DestroyWindow(g.captionTooltips);
        g.captionTooltips = nullptr;
        SetCursorPos(neutral.x, neutral.y);
        Pump(30);
        // Independent DPI simulations exercise fonts/icons/layout without changing
        // the monitor or modifying the user's Windows display settings.
        for (UINT dpi : {96u, 144u, 192u}) {
            g.dpi = dpi;
            CreateFonts();
            RefreshThemeIcons();
            int width = std::min(Scale(1080), static_cast<int>(monitor.rcWork.right) - left - 20);
            int height = std::min(Scale(310), static_cast<int>(monitor.rcWork.bottom) - top - 20);
            SetWindowPos(host, HWND_TOP, left, top, width, height, SWP_NOACTIVATE);
            Layout();
            for (bool dark : {false, true}) {
                g.darkMode = dark;
                ApplyDwmFrame(host);
                SetCaptionSample(.5, false);
                Pixels scaled = CaptureOwnedWindow(host, report);
                RECT bounds = VisibleFrame(host);
                POINT origin{}; ClientToScreen(host, &origin);
                captionRows.push_back(Crop(scaled, {origin.x - bounds.left, origin.y - bounds.top,
                    scaled.width, origin.y - bounds.top + Scale(34)}));
                report << "simulated_dpi=" << dpi << " dark=" << dark
                    << " title_font_height=" << -MulDiv(9, static_cast<int>(dpi), 72)
                    << " icon_size=" << Scale(18) << '\n';
            }
        }
        save(L"caption-states-and-dpi.png", Stack(captionRows));
        report << "mock_startup_changes=" << visualStartupChanges << " (expected zero)\n";
        if (visualStartupChanges != 0 && !failure) failure = 99;
        ExitApplication();
        Pump(20);
    }
    SetCursorPos(originalCursor.x, originalCursor.y);
    if (originalForeground && IsWindow(originalForeground)) SetForegroundWindow(originalForeground);
    report << "exit_code=" << failure << '\n';
    factory.Reset();
    CoUninitialize();
    std::printf("CAPTION_VISUAL=%d\n", failure);
    return failure;
}
