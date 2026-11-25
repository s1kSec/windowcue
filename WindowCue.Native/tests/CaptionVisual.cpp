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
    RECT visibleBounds{};
    if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleBounds, sizeof(visibleBounds))) &&
        visibleBounds.right > visibleBounds.left && visibleBounds.bottom > visibleBounds.top)
        bounds = visibleBounds;
    return bounds;
}

Pixels CaptureOwnedWindowOnce(HWND window, std::ofstream& report, bool settle = true) {
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window)) return {};
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (settle) UpdateWindow(window);
    DwmFlush();
    if (settle) Sleep(50);
    RECT bounds = VisibleFrame(window);
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return {};
    for (int y : {8, height / 2, height - 9}) {
        for (int x : {8, width / 2, width - 9}) {
            if (x < 0 || y < 0 || x >= width || y >= height) continue;
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
        method = "WM_PRINTCLIENT-fallback";
        SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(destination), PRF_CLIENT);
        copied = TRUE;
    }
    Pixels pixels;
    if (copied) {
        pixels = Pixels{width, height, std::vector<BYTE>(static_cast<size_t>(width) * height * 4)};
        std::memcpy(pixels.bgra.data(), bits, pixels.bgra.size());
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
    Pixels result{width, height, std::vector<BYTE>(static_cast<size_t>(width) * height * 4, 210)};
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

LRESULT CALLBACK PreviewFixture(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{};
        GetClientRect(hwnd, &r);
        HBRUSH brush = CreateSolidBrush(RGB(37, 151, 204));
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

// Sample immediately after each synchronous nonclient operation, BEFORE a
// WM_PAINT can cover a system-drawn caption or uninitialized black popover.
int CheckChromeTransitions(HWND host, IWICImagingFactory* factory,
        const std::filesystem::path& output, std::ofstream& report) {
    using namespace windowcue;
    int failures{}, samples{};
    auto geometry = [&](HWND window, const char* name) {
        RECT outer{}, client{};
        GetWindowRect(window, &outer);
        GetClientRect(window, &client);
        POINT origin{};
        ClientToScreen(window, &origin);
        RECT proposed{10, 20, 810, 420}, calculated = proposed;
        SendMessageW(window, WM_NCCALCSIZE, FALSE, reinterpret_cast<LPARAM>(&calculated));
        bool full = origin.x == outer.left && origin.y == outer.top &&
            client.right == outer.right - outer.left && client.bottom == outer.bottom - outer.top;
        bool initial = EqualRect(&calculated, &proposed) != FALSE;
        NCCALCSIZE_PARAMS resize{};
        resize.rgrc[0] = proposed;
        SendMessageW(window, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&resize));
        bool subsequent = EqualRect(&resize.rgrc[0], &proposed) != FALSE;
        report << "chrome_geometry " << name << " full_client=" << full
               << " initial_nccalc=" << initial << " resize_nccalc=" << subsequent << '\n';
        if (!full || !initial || !subsequent) ++failures;
    };

    auto pixelMatches = [](const Pixels& pixels, int x, int y, COLORREF expected) {
        if (x < 0 || y < 0 || x >= pixels.width || y >= pixels.height) return false;
        size_t at = (static_cast<size_t>(y) * pixels.width + x) * 4;
        return pixels.bgra[at] == GetBValue(expected) &&
               pixels.bgra[at + 1] == GetGValue(expected) &&
               pixels.bgra[at + 2] == GetRValue(expected);
    };

    auto sampleMain = [&](HWND window, const char* label) {
        auto pixels = CaptureOwnedWindowOnce(window, report, false);
        RECT frame = VisibleFrame(window);
        POINT origin{};
        ClientToScreen(window, &origin);
        UINT dpi = g.dpi;
        int x = origin.x - frame.left + MulDiv(160, dpi, 96);
        int y = origin.y - frame.top + MulDiv(16, dpi, 96);
        COLORREF expected = P().windowBackground;
        bool good = pixels.valid();
        for (int dy = 0; good && dy < 5; ++dy) {
            for (int dx = 0; dx < 12; ++dx) {
                if (!pixelMatches(pixels, x + dx, y + dy, expected)) { good = false; break; }
            }
        }
        // Probe just inside all four edges (3px inward clears the 1px Win11 DWM border).
        for (POINT p : {POINT{160, 3}, POINT{3, 60}, POINT{pixels.width - 4, 60},
                        POINT{160, pixels.height - 4}}) {
            if (!good || !pixelMatches(pixels, p.x, p.y, expected)) { good = false; break; }
        }
        report << "chrome_immediate " << label << " dark=" << g.darkMode << " match=" << good << '\n';
        ++samples;
        if (!good) {
            ++failures;
            WritePng(factory, output / (L"chrome-failure-" + std::to_wstring(samples) + L".png"), pixels);
        }
    };

    auto sampleSettingsImmediate = [&](const char* label) {
        auto pixels = CaptureOwnedWindowOnce(g.settingsWindow, report, false);
        COLORREF expected = P().settingsSurface;
        bool good = pixels.valid();
        // Top padding band of the floating popover (y = 4..7, clear of row 1 at y = 14 DIP)
        for (int dy = 4; good && dy < 8; ++dy) {
            for (int dx = 20; dx < 80; dx += 4) {
                if (!pixelMatches(pixels, dx, dy, expected)) { good = false; break; }
            }
        }
        // Bottom padding band (clear of footer & 1px native DWM border)
        for (int dx = 20; good && dx < 80; dx += 4) {
            if (!pixelMatches(pixels, dx, pixels.height - 4, expected)) { good = false; break; }
        }
        report << "settings_immediate_no_black " << label << " dark=" << g.darkMode
               << " match=" << good << '\n';
        ++samples;
        if (!good) {
            ++failures;
            WritePng(factory, output / (L"settings-failure-" + std::to_wstring(samples) + L".png"), pixels);
        }
    };

    bool style = (GetWindowLongPtrW(host, GWL_STYLE) & WS_CAPTION) == 0 &&
        (GetWindowLongPtrW(host, GWL_STYLE) & WS_THICKFRAME) != 0;
    report << "chrome_style caption_absent_resize_present=" << style
           << " main_native_rounded=" << g.chromeStatus.nativeRounded()
           << " main_native_border=" << g.chromeStatus.nativeBorder()
           << " main_d2d_hr=0x" << std::hex << g.renderer.LastError() << std::dec
           << " main_d2d_gen=" << g.renderer.Generation() << '\n';
    if (!style) ++failures;
    geometry(host, "main");

    for (bool dark : {false, true}) {
        g.darkMode = dark;
        ApplyDwmFrame(host);
        SetCaptionSample(0, false);
        sampleMain(host, "baseline");
        SetWindowTextW(host, L"WindowCue visual smoke");
        sampleMain(host, "set-text");
        SendMessageW(host, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g.appIcon));
        sampleMain(host, "set-icon");
        for (bool active : {false, true}) {
            SendMessageW(host, WM_NCACTIVATE, active, 0);
            sampleMain(host, active ? "activate" : "deactivate");
        }
        SendMessageW(host, WM_NCPAINT, 1, 0);
        sampleMain(host, "nc-paint");
        SetWindowPos(host, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        sampleMain(host, "frame-changed");
        for (RECT button : {g.minButtonRect, g.maxButtonRect, g.closeButtonRect}) {
            POINT p{(button.left + button.right) / 2, (button.top + button.bottom) / 2};
            ClientToScreen(host, &p);
            auto hit = SendMessageW(host, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y));
            SendMessageW(host, WM_NCMOUSEMOVE, hit, MAKELPARAM(p.x, p.y));
            sampleMain(host, "nc-hover");
            SendMessageW(host, WM_NCLBUTTONDOWN, hit, MAKELPARAM(p.x, p.y));
            sampleMain(host, "nc-press");
            ReleaseCapture();
            SendMessageW(host, WM_CANCELMODE, 0, 0);
            UpdateWindow(host);
        }
        SendMessageW(host, WM_NCMOUSELEAVE, 0, 0);
        UpdateWindow(host);

        // Verify zero black flash on first & repeated Settings open, AND verify
        // automatic light-dismiss when activating the main window.
        for (int cycle = 0; cycle < 3; ++cycle) {
            ShowSettingsWindow();
            if (cycle == 0) {
                geometry(g.settingsWindow, "settings");
                report << "settings_native_rounded=" << g.settingsChromeStatus.nativeRounded()
                       << " settings_native_border=" << g.settingsChromeStatus.nativeBorder() << '\n';
            }
            // Capture IMMEDIATELY with settle=false: must already be themed, never black!
            sampleSettingsImmediate(cycle == 0 ? "first-open" : "reopen");
            // Activating main window must auto-dismiss the Settings popover.
            SetActiveWindow(host);
            bool autoClosed = !IsWindowVisible(g.settingsWindow);
            report << "settings_auto_dismiss cycle=" << cycle << " closed=" << autoClosed << '\n';
            if (!autoClosed) ++failures;
            sampleMain(host, "handoff-main");
        }

        auto clickMaximize = [&]() {
            RECT r = g.maxButtonRect;
            POINT p{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
            ClientToScreen(host, &p);
            SendMessageW(host, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(p.x, p.y));
            SendMessageW(host, WM_NCLBUTTONUP, HTMAXBUTTON, MAKELPARAM(p.x, p.y));
            UpdateWindow(host);
            DwmFlush();
        };
        clickMaximize();
        bool maximized = IsZoomed(host) != FALSE;
        geometry(host, "maximized");
        sampleMain(host, "maximized");
        clickMaximize();
        bool restored = !IsZoomed(host);
        geometry(host, "restored");
        sampleMain(host, "restored");
        POINT corner{1, 1};
        ClientToScreen(host, &corner);
        bool resize = SendMessageW(host, WM_NCHITTEST, 0, MAKELPARAM(corner.x, corner.y)) == HTTOPLEFT;
        report << "chrome_behaviors maximized=" << maximized << " restored=" << restored
               << " resize_hit=" << resize << " system_menu=" << (GetSystemMenu(host, FALSE) != nullptr) << '\n';
        if (!maximized || !restored || !resize || !GetSystemMenu(host, FALSE)) ++failures;
    }
    report << "chrome_transition_samples=" << samples << " failures=" << failures << '\n';
    return failures;
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace windowcue;
    if (argc != 2 || GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", nullptr, 0) < 2) return 90;
    const std::filesystem::path output(argv[1]);
    if (!std::filesystem::is_directory(output) || std::filesystem::exists(output / L"caption-report.txt")) return 91;
    std::ofstream report(output / L"caption-report.txt");
    report << "Actual native UI, production Main.cpp/Direct2D paths; startup API mocked.\n"
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
    g.appLargeIcon = static_cast<HICON>(LoadImageW(g.instance, MAKEINTRESOURCEW(IDI_WINDOWCUE), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR));
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
    g.resizingForContent = true;
    HWND host = failure ? nullptr : CreateWindowExW(WS_EX_APPWINDOW, kWindowClass, L"WindowCue visual smoke",
        kMainWindowStyle, left, top, 1080, 320, nullptr, nullptr, g.instance, nullptr);
    if (!host) failure = 95;
    const UINT actualDpi = g.dpi;
    report << "monitor_dpi=" << actualDpi << '\n';
    auto save = [&](const wchar_t* name, const Pixels& pixels) {
        if (!WritePng(factory.Get(), output / name, pixels) && !failure) failure = 96;
    };
    std::vector<Pixels> captionRows;
    std::vector<Pixels> settingsRows;
    if (host) {
        RevealWindowWithoutFlash(host, left, top, true, false);
        SetWindowPos(host, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetForegroundWindow(host);
        POINT neutral{50, 100};
        ClientToScreen(host, &neutral);
        SetCursorPos(neutral.x, neutral.y);
        Pump(120);
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
                    x + g.themeButtonRect.right + Scale(8), y + Scale(kTitleHeightDip)}));
                report << (dark ? "dark" : "light") << " hover_level=" << level << '\n';
            }
            SetCaptionSample(1, true);
            Pixels pressed = CaptureOwnedWindow(host, report);
            RECT wr = VisibleFrame(host);
            POINT origin{};
            ClientToScreen(host, &origin);
            captionRows.push_back(Crop(pressed, {origin.x - wr.left + g.settingsButtonRect.left - Scale(8),
                origin.y - wr.top, origin.x - wr.left + g.themeButtonRect.right + Scale(8),
                origin.y - wr.top + Scale(kTitleHeightDip)}));
            SetCaptionSample(0, false);

            ShowSettingsWindow();
            // Capture immediately with zero settle delay to verify zero black screen!
            Pixels immediateSettings = CaptureOwnedWindowOnce(g.settingsWindow, report, false);
            save(dark ? L"dark-settings.png" : L"light-settings.png", immediateSettings);
            save(dark ? L"dark-review.png" : L"light-review.png", Stack({main, immediateSettings}));
            settingsRows.push_back(immediateSettings);

            RECT compactClient{};
            GetClientRect(g.settingsWindow, &compactClient);
            report << (dark ? "dark" : "light") << " settings_client="
                << RectWidth(compactClient) << 'x' << RectHeight(compactClient)
                << " has_message=" << (!g.settingsMessage.empty()) << '\n';
            if ((RectWidth(compactClient) != SettingsScale(kSettingsWidth) ||
                 RectHeight(compactClient) > SettingsScale(130) ||
                 !g.settingsMessage.empty()) && !failure) failure = 100;

            for (StartupState state : {StartupState::Disabled, StartupState::DisabledByUser}) {
                visualStartupStatus.state = state;
                RefreshStartupControls();
                Pump(40);
                Pixels sample = CaptureOwnedWindow(g.settingsWindow, report);
                bool blocked = state == StartupState::DisabledByUser;
                save(dark ? (blocked ? L"dark-settings-windows-disabled.png" : L"dark-settings-off.png") :
                     (blocked ? L"light-settings-windows-disabled.png" : L"light-settings-off.png"), sample);
                settingsRows.push_back(sample);
                RECT stateClient{};
                GetClientRect(g.settingsWindow, &stateClient);
                report << (dark ? "dark" : "light") << " settings_blocked=" << blocked
                    << " client=" << RectWidth(stateClient) << 'x' << RectHeight(stateClient)
                    << " has_message=" << (!g.settingsMessage.empty()) << '\n';
                if (((!g.settingsMessage.empty()) != blocked ||
                     (blocked && RectHeight(stateClient) <= RectHeight(compactClient))) && !failure)
                    failure = 101;
            }
            visualStartupStatus.state = StartupState::Enabled;
            RefreshStartupControls();
            HideSettingsWindow();
        }
        save(L"settings-states.png", Stack(settingsRows));

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

        SetForegroundWindow(host);
        POINT gear{(g.settingsButtonRect.left + g.settingsButtonRect.right) / 2,
                   (g.settingsButtonRect.top + g.settingsButtonRect.bottom) / 2};
        ClientToScreen(host, &gear);
        SetCursorPos(neutral.x, neutral.y);
        Pump(50);
        SetCursorPos(gear.x, gear.y);
        Pump(900);
        if (!IsWindowVisible(g.captionTooltips)) {
            POINT clientGear = gear;
            ScreenToClient(host, &clientGear);
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
        SendMessageW(g.captionTooltips, TTM_ACTIVATE, FALSE, 0);
        DestroyWindow(g.captionTooltips);
        g.captionTooltips = nullptr;
        SetCursorPos(neutral.x, neutral.y);
        Pump(30);

        for (UINT dpi : {96u, 144u, 192u}) {
            g.dpi = dpi;
            CreateFonts();
            RefreshThemeIcons();
            int width = std::min(Scale(1080), static_cast<int>(monitor.rcWork.right) - left - 20);
            int height = std::min(Scale(320), static_cast<int>(monitor.rcWork.bottom) - top - 20);
            SetWindowPos(host, HWND_TOP, left, top, width, height, SWP_NOACTIVATE);
            Layout();
            for (bool dark : {false, true}) {
                g.darkMode = dark;
                ApplyDwmFrame(host);
                SetCaptionSample(.5, false);
                Pixels scaled = CaptureOwnedWindow(host, report);
                RECT bounds = VisibleFrame(host);
                POINT origin{};
                ClientToScreen(host, &origin);
                captionRows.push_back(Crop(scaled, {origin.x - bounds.left, origin.y - bounds.top,
                    scaled.width, origin.y - bounds.top + Scale(kTitleHeightDip)}));
                report << "simulated_dpi=" << dpi << " dark=" << dark
                       << " icon_size=" << Scale(16) << '\n';
            }
        }
        save(L"caption-states-and-dpi.png", Stack(captionRows));

        // Complete UI scene with DWM live preview, bound cards with hotkey badges,
        // selected card + tactile keycaps in dock.
        g.dpi = actualDpi;
        CreateFonts();
        g.renderer.Discard();
        WNDCLASSW fixtureClass{};
        fixtureClass.hInstance = g.instance;
        fixtureClass.lpfnWndProc = PreviewFixture;
        fixtureClass.lpszClassName = L"WindowCue.VisualFixture";
        RegisterClassW(&fixtureClass);
        HWND source = CreateWindowExW(WS_EX_TOOLWINDOW, fixtureClass.lpszClassName, L"Native preview fixture",
            WS_POPUP, monitor.rcWork.left + 30, monitor.rcWork.top + 600, 640, 400, nullptr, nullptr, g.instance, nullptr);
        if (!source && !failure) failure = 102;
        if (source) {
            ShowWindow(source, SW_SHOWNOACTIVATE);
            UpdateWindow(source);
            g.windows.clear();
            PrependWindowCueItem(g.windows);
            WindowInfo fixture;
            fixture.handle = source;
            fixture.title = L"Live preview";
            fixture.processPath = L"VectorFixture.exe";
            g.windows.push_back(std::move(fixture));
            ActiveBinding binding;
            binding.target = host;
            binding.settings.isWindowCue = true;
            binding.settings.modifiers = MOD_ALT;
            binding.settings.virtualKey = 'P';
            binding.settings.hotkeyText = L"Alt + P";
            g.bindings.push_back(binding);
            g.windows[0].bound = true;
            g.selected = 0;
            g.captured = true;
            g.capturedModifiers = MOD_ALT;
            g.capturedKey = 'P';
            g.inputText = L"Alt + P";
            g.hovered = 1;
            SetWindowPos(host, HWND_TOPMOST, left, top, Scale(1080), Scale(420), SWP_NOACTIVATE);
            RegisterThumbnails();
            Layout();
            UpdateThumbnails();
            for (int sample = 0; sample < 20; ++sample) {
                g.darkMode = (sample % 2) != 0;
                ApplyDwmFrame(host);
                SetCaptionSample(0, false);
                UpdateThumbnails();
                Pump(50);
                DwmFlush();
                auto pixels = CaptureOwnedWindow(host, report);
                if (sample < 2) save(g.darkMode ? L"dark-fixture.png" : L"light-fixture.png", pixels);
                RECT frame = VisibleFrame(host);
                POINT origin{};
                ClientToScreen(host, &origin);
                RECT preview = g.windows[1].appliedThumbnailRect;
                int x = origin.x - frame.left + (preview.left + preview.right) / 2;
                int y = origin.y - frame.top + (preview.top + preview.bottom) / 2;
                bool valid = pixels.valid() && x >= 0 && y >= 0 && x < pixels.width && y < pixels.height;
                if (valid) {
                    size_t at = (static_cast<size_t>(y) * pixels.width + x) * 4;
                    valid = pixels.bgra[at] == 204 && pixels.bgra[at + 1] == 151 && pixels.bgra[at + 2] == 37;
                }
                report << "vector_preview_theme_sample=" << sample << " pixel_match=" << valid << '\n';
                if (!valid && !failure) failure = 103;
            }
            g.hovered = -1;
            g.renderer.ForgetImages();
            ReleaseWindows(g.windows);
            g.bindings.clear();
            DestroyWindow(source);
        }
        report << "mock_startup_changes=" << visualStartupChanges << " (expected zero)\n";
        if (CheckChromeTransitions(host, factory.Get(), output, report) && !failure) failure = 104;
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
