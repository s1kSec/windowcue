// Keep regression-only entry points out of the release EXEs. Including the
// production TU lets these checks exercise the actual message handlers.
#include "../src/WindowCueNative.h"

namespace {
struct ThumbnailSubmission {
    HTHUMBNAIL thumbnail;
    DWM_THUMBNAIL_PROPERTIES properties;
};
std::vector<ThumbnailSubmission> submissions;

HRESULT WINAPI RecordThumbnailProperties(
    HTHUMBNAIL thumbnail, const DWM_THUMBNAIL_PROPERTIES* properties) {
    HRESULT result = DwmUpdateThumbnailProperties(thumbnail, properties);
    if (SUCCEEDED(result)) submissions.push_back({thumbnail, *properties});
    return result;
}
}

// Exercise the settings message handlers without touching Windows startup
// registration or opening a browser. The production backend has its own tests.
namespace windowcue {
StartupStatus regressionStartupStatus{StartupState::Disabled, L"Regression startup status"};
int regressionStartupChanges = 0;
StartupStatus QueryRegressionStartupStatus() { return regressionStartupStatus; }
StartupStatus SetRegressionStartupEnabled(bool enabled) {
    ++regressionStartupChanges;
    regressionStartupStatus.state = enabled ? StartupState::Enabled : StartupState::Disabled;
    return regressionStartupStatus;
}
}

#define DwmUpdateThumbnailProperties RecordThumbnailProperties
#define QueryStartupStatus QueryRegressionStartupStatus
#define SetStartupEnabled SetRegressionStartupEnabled
#define wWinMain WindowCueProductionEntry
#include "../src/Main.cpp"
#undef wWinMain
#undef SetStartupEnabled
#undef QueryStartupStatus
#undef DwmUpdateThumbnailProperties

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace windowcue;
    // The harness must never use or overwrite the user's real configuration.
    if (GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", nullptr, 0) < 2) return 90;
    EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g.instance = instance;
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProcedure;
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassW(&wc)) return 91;
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"WindowCue regression",
        kMainWindowStyle, -32000, -32000, 1080, 610,
        nullptr, nullptr, instance, nullptr);
    HWND source = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Preview source",
        WS_POPUP, -32000, -32000, 640, 480,
        nullptr, nullptr, instance, nullptr);
    if (!host || !source) return 92;
    HWND originalForeground = GetForegroundWindow();

    int failure = 0;
    auto check = [&](bool ok, int code) { if (!ok && !failure) failure = code; };
    check(!IsWindowVisible(host), 58);

    // Light mode must be comfortably toned down (matte low-glare) and pass WCAG AA.
    g.darkMode = false;
    check(RelativeLuminance(P().windowBackground) <= 0.84 &&
          RelativeLuminance(P().cardBackground) <= 0.88 &&
          ValidateThemePalettes(), 59);
    g.darkMode = true;

    // A fresh configuration creates Alt+P and persists the semantic self target.
    LoadBindings();
    int selfIndex = BindingIndexForWindow(host);
    check(selfIndex >= 0, 10);
    if (selfIndex >= 0) {
        const auto& settings = g.bindings[selfIndex].settings;
        check(settings.isWindowCue && settings.modifiers == MOD_ALT &&
              settings.virtualKey == 'P' && settings.hotkeyText == L"Alt + P", 11);
    }
    auto saved = LoadSettings();
    check(saved.size() == 1 && saved[0].isWindowCue &&
          saved[0].virtualKey == 'P' && IsSelfHotkeyInitialized(), 12);
    UnregisterAllBindings();
    g.bindings.clear();

    // An existing Alt+P assignment to another window must not be stolen.
    wchar_t isolatedDirectory[32768]{};
    GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", isolatedDirectory,
                            static_cast<DWORD>(std::size(isolatedDirectory)));
    std::wstring conflictDirectory = std::wstring(isolatedDirectory) + L"\\conflict";
    CreateDirectoryW(conflictDirectory.c_str(), nullptr);
    SetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", conflictDirectory.c_str());
    ActiveBinding occupied;
    occupied.target = source;
    occupied.settings.processPath = CurrentExecutablePath();
    occupied.settings.modifiers = MOD_ALT;
    occupied.settings.virtualKey = 'P';
    g.bindings.push_back(occupied);
    LoadBindings();
    check(g.bindings.size() == 1 && g.bindings[0].target == source &&
          BindingIndexForWindow(host) < 0, 18);
    g.bindings.clear();
    SetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", isolatedDirectory);

    // A changed HWND must not make the saved WindowCue hotkey stale.
    if (!saved.empty()) {
        ActiveBinding stale;
        stale.settings = saved[0];
        stale.target = source;
        check(SaveSettings({stale}), 13);
        LoadBindings();
        check(BindingIndexForWindow(host) >= 0 && BindingIndexForWindow(source) < 0, 14);
        RemoveBinding(static_cast<size_t>(BindingIndexForWindow(host)));
        LoadBindings();
        check(g.bindings.empty(), 15); // Explicit cancellation survives restart.
        stale.settings.modifiers = MOD_CONTROL | MOD_ALT;
        stale.settings.virtualKey = VK_F24;
        stale.settings.hotkeyText = L"Ctrl + Alt + F24";
        check(SaveSettings({stale}), 16);
        LoadBindings();
        check(g.bindings.size() == 1 && g.bindings[0].settings.virtualKey == VK_F24, 17);
    }
    UnregisterAllBindings();
    g.bindings.clear();

    // Exercise the actual client layout at common DPI scales, including the
    // exact width at which another column fits.
    UINT actualDpi = g.dpi;
    for (UINT dpi : {96u, 120u, 144u, 192u}) {
        g.dpi = dpi;
        GridLayoutMetrics metrics;
        int fiveColumnWidth = metrics.margin * 2 + metrics.previewInset * 2 +
            metrics.scrollbarSpace + metrics.cardWidth * 5 + metrics.cardGap * 4;
        check(metrics.Columns(fiveColumnWidth) == 5 &&
              metrics.Columns(fiveColumnWidth - 1) == 4, 50);
        for (int width : {Scale(1080), fiveColumnWidth - 1, fiveColumnWidth}) {
            for (size_t count : {size_t{0}, size_t{1}, size_t{10}, size_t{11},
                                 size_t{15}, size_t{16}, size_t{60}}) {
                g.windows.clear();
                g.windows.resize(count);
                g.scrollOffset = 100000;
                SetWindowPos(host, nullptr, -32000, -32000, width, Scale(320),
                             SWP_NOZORDER | SWP_NOACTIVATE);
                ResizeForContent();
                Layout();
                check(g.settingsButtonRect.right == g.themeButtonRect.left &&
                      RectWidth(g.settingsButtonRect) == Scale(34), 60);
                POINT settingsPoint{g.settingsButtonRect.left + Scale(10),
                                     g.settingsButtonRect.top + Scale(10)};
                ClientToScreen(host, &settingsPoint);
                check(HitTest(settingsPoint) == HTCLIENT, 61);
                RECT bounds{};
                GetWindowRect(host, &bounds);
                MONITORINFO monitor{sizeof(monitor)};
                check(GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST),
                                      &monitor) != FALSE, 51);
                check(bounds.top >= monitor.rcWork.top &&
                      bounds.bottom <= monitor.rcWork.bottom, 52);
                if (g.maxScroll) {
                    check(RectHeight(bounds) == RectHeight(monitor.rcWork), 53);
                    check(g.scrollOffset == g.maxScroll &&
                          g.windows.back().cardRect.bottom <= g.previewInner.bottom, 54);
                } else {
                    check(g.scrollOffset == 0, 55);
                    for (size_t i = 0; i < count; ++i) {
                        const RECT& card = g.windows[i].cardRect;
                        check(card.top >= g.previewInner.top &&
                              card.bottom <= g.previewInner.bottom &&
                              HitCard({card.left + 10, card.bottom - 1}) ==
                                  static_cast<int>(i), 56);
                    }
                }
            }
        }
    }
    g.dpi = actualDpi;
    g.windows.resize(11);
    g.resizingForContent = true;
    SetWindowPos(host, nullptr, -32000, -32000, Scale(1080), Scale(320),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    g.resizingForContent = false;
    SendMessageW(host, WM_SIZE, SIZE_RESTORED, 0);
    check(g.maxScroll == 0 &&
          g.windows.back().cardRect.bottom <= g.previewInner.bottom, 57);
    g.windows.clear();
    g.scrollOffset = 0;
    SetWindowPos(host, nullptr, -32000, -32000, 1080, 610,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    PrependWindowCueItem(g.windows);
    WindowInfo preview;
    preview.handle = source;
    preview.title = L"Preview source";
    preview.processPath = CurrentExecutablePath();
    g.windows.push_back(std::move(preview));
    RegisterThumbnails();
    Layout();
    check(g.windows[0].isWindowCue && !g.windows[0].thumbnail, 20);
    HTHUMBNAIL thumbnail = g.windows[1].thumbnail;
    check(thumbnail != nullptr, 21);
    ShowWindow(source, SW_SHOWNOACTIVATE);
    ShowWindow(host, SW_SHOWNOACTIVATE);
    UpdateWindow(host);
    UpdateThumbnails();
    check(g.windows[1].thumbnailVisible, 29);

    // Tooltips remain native, localized, and aligned with the hit regions.
    check(g.captionTooltips && SendMessageW(g.captionTooltips, TTM_GETTOOLCOUNT, 0, 0) == 2, 80);
    if (g.captionTooltips) {
        wchar_t tipText[256]{};
        TOOLINFOW tip{sizeof(tip)};
        tip.hwnd = host;
        tip.uId = kSettingsTooltipId;
        tip.lpszText = tipText;
        check(SendMessageW(g.captionTooltips, TTM_GETTOOLINFOW, 0,
                           reinterpret_cast<LPARAM>(&tip)) &&
              SameRect(tip.rect, g.settingsButtonRect) &&
              std::wstring(tipText) == T().settingsTooltip, 81);
    }

    // Hover path invalidates only caption controls; reduced motion settles immediately.
    bool animationPolicy = g.captionAnimationsAllowed;
    ResetCaptionInteraction();
    ValidateRect(host, nullptr);
    g.captionAnimationsAllowed = false;
    SetCaptionHovered(true, true);
    RECT captionUpdate{};
    check(GetUpdateRect(host, &captionUpdate, FALSE) &&
          SameRect(captionUpdate, g.settingsButtonRect), 82);
    check(g.settingsHoverAnimation.value == 1.0 && !g.captionAnimationRunning, 83);
    g.captionAnimationsAllowed = true;
    SetCaptionHovered(true, false);
    check(g.captionAnimationRunning && g.settingsHoverAnimation.target == 0.0, 84);
    AdvanceCaptionHover(g.settingsHoverAnimation,
                        g.settingsHoverAnimation.started + kCaptionHoverDuration);
    SyncCaptionAnimationTimer();
    check(g.settingsHoverAnimation.value == 0.0 && !g.captionAnimationRunning, 85);
    ResetCaptionInteraction();
    g.captionAnimationsAllowed = animationPolicy;

    // Exercise the single-HWND floating Settings popover: zero child swap-chains,
    // keyboard navigation, state expansion, UIA, and automatic light-dismiss.
    POINT gearPoint{g.settingsButtonRect.left + Scale(10), Scale(16)};
    SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(gearPoint.x, gearPoint.y));
    SendMessageW(host, WM_LBUTTONUP, 0, MAKELPARAM(gearPoint.x, gearPoint.y));
    check(g.settingsWindow && IsWindowVisible(g.settingsWindow) &&
          GetWindow(g.settingsWindow, GW_OWNER) == host, 62);
    check(g.renderer.ValidateGeometry() && !g.renderingFailed, 63);

    if (g.settingsWindow) {
        RECT compactClient{};
        GetClientRect(g.settingsWindow, &compactClient);
        check(RectWidth(compactClient) == SettingsScale(kSettingsWidth) &&
              RectHeight(compactClient) <= SettingsScale(130), 100);
        check(RectWidth(g.settingsSwitchRect) == SettingsScale(38) &&
              RectHeight(g.settingsSwitchRect) == SettingsScale(20), 101);
        check(RectWidth(g.settingsGitHubRect) == SettingsScale(30) &&
              RectHeight(g.settingsGitHubRect) == SettingsScale(30), 102);
        check(g.settingsMessage.empty(), 103);
        // Single-HWND popover: zero child HWNDs means zero extra D2D swap-chains.
        check(GetWindow(g.settingsWindow, GW_CHILD) == nullptr, 104);

        // Clicking startup switch toggles startup cleanly.
        POINT switchCenter{(g.settingsSwitchRect.left + g.settingsSwitchRect.right) / 2,
                           (g.settingsSwitchRect.top + g.settingsSwitchRect.bottom) / 2};
        SendMessageW(g.settingsWindow, WM_LBUTTONDOWN, MK_LBUTTON,
                     MAKELPARAM(switchCenter.x, switchCenter.y));
        SendMessageW(g.settingsWindow, WM_LBUTTONUP, 0,
                     MAKELPARAM(switchCenter.x, switchCenter.y));
        check(regressionStartupChanges == 1 && g.startupStatus.state == StartupState::Enabled &&
              g.settingsMessage.empty(), 65);

        // Enter key toggles the focused switch back to disabled.
        g.settingsFocusIndex = 0;
        MSG enter{};
        enter.hwnd = g.settingsWindow;
        enter.message = WM_KEYDOWN;
        enter.wParam = VK_RETURN;
        check(HandleSettingsMessage(enter) && regressionStartupChanges == 2 &&
              g.startupStatus.state == StartupState::Disabled &&
              g.settingsMessage.empty(), 67);

        // Windows-managed / policy / unavailable states expand banner & restore cleanly.
        for (StartupState state : {StartupState::DisabledByUser, StartupState::DisabledByPolicy,
                                   StartupState::Unavailable}) {
            regressionStartupStatus.state = state;
            RefreshStartupControls();
            check(CanInteractWithStartup() == (state == StartupState::DisabledByUser), 68);
            check(g.settingsMessage.find(StartupStateText(state)) == 0, 70);
            RECT warningClient{};
            GetClientRect(g.settingsWindow, &warningClient);
            check(RectHeight(warningClient) > RectHeight(compactClient) &&
                  RectWidth(warningClient) == RectWidth(compactClient), 105);
        }
        regressionStartupStatus.state = StartupState::Disabled;
        RefreshStartupControls();
        RECT restoredClient{};
        GetClientRect(g.settingsWindow, &restoredClient);
        check(g.settingsMessage.empty() && SameRect(restoredClient, compactClient), 106);

        // Tab cycles focus to GitHub; Escape closes Settings.
        g.settingsFocusIndex = 0;
        MSG tab{};
        tab.hwnd = g.settingsWindow;
        tab.message = WM_KEYDOWN;
        tab.wParam = VK_TAB;
        check(HandleSettingsMessage(tab) && g.settingsFocusIndex == 1, 71);
        MSG escape = tab;
        escape.wParam = VK_ESCAPE;
        check(HandleSettingsMessage(escape) && !IsWindowVisible(g.settingsWindow), 72);

        // Reopen via Ctrl+, (must NOT trigger while capturing a hotkey).
        BYTE originalKeys[256]{};
        BYTE controlKeys[256]{};
        GetKeyboardState(originalKeys);
        std::copy(std::begin(originalKeys), std::end(originalKeys), std::begin(controlKeys));
        controlKeys[VK_CONTROL] |= 0x80;
        SetKeyboardState(controlKeys);
        BeginCapture();
        SendMessageW(host, WM_KEYDOWN, VK_OEM_COMMA, 0);
        check(g.capturing && g.captured && g.capturedModifiers == MOD_CONTROL &&
              g.capturedKey == VK_OEM_COMMA && !IsWindowVisible(g.settingsWindow), 76);
        EndCapture(false);
        SendMessageW(host, WM_KEYDOWN, VK_OEM_COMMA, 0);
        SetKeyboardState(originalKeys);
        check(IsWindowVisible(g.settingsWindow), 73);

        // Auto-close #1: clicking the main window's business area hides Settings.
        POINT businessPoint{g.previewInner.left + Scale(20), g.previewInner.top + Scale(20)};
        SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(businessPoint.x, businessPoint.y));
        SendMessageW(host, WM_LBUTTONUP, 0, MAKELPARAM(businessPoint.x, businessPoint.y));
        check(!IsWindowVisible(g.settingsWindow), 114);

        // Auto-close #2: losing activation (WA_INACTIVE) hides Settings.
        ShowSettingsWindow();
        check(IsWindowVisible(g.settingsWindow), 115);
        SendMessageW(g.settingsWindow, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(host));
        check(!IsWindowVisible(g.settingsWindow), 116);

        // Gear button toggle: clicking gear while Settings is open closes cleanly.
        ShowSettingsWindow();
        check(IsWindowVisible(g.settingsWindow), 117);
        SendMessageW(host, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(gearPoint.x, gearPoint.y));
        SendMessageW(host, WM_LBUTTONUP, 0, MAKELPARAM(gearPoint.x, gearPoint.y));
        check(!IsWindowVisible(g.settingsWindow), 118);

        // Verify UI Automation fragment root + StartupSwitch ToggleState.
        ShowSettingsWindow();
        for (bool dark : {false, true}) {
            g.darkMode = dark;
            ApplySettingsTheme();
            UpdateWindow(g.settingsWindow);
            check(ValidateThemePalettes(), 74);
            check(g.settingsRenderer.ValidateGeometry() && !g.renderingFailed, 107);
        }
        auto* rootAuto = new SettingsPopoverAutomation(&gSettingsAccessibility);
        IRawElementProviderFragment* firstChild = nullptr;
        check(SUCCEEDED(rootAuto->Navigate(NavigateDirection_FirstChild, &firstChild)) &&
              firstChild != nullptr, 108);
        if (firstChild) {
            IToggleProvider* toggleProvider = nullptr;
            check(SUCCEEDED(firstChild->QueryInterface(__uuidof(IToggleProvider),
                                                       reinterpret_cast<void**>(&toggleProvider))) &&
                  toggleProvider != nullptr, 109);
            if (toggleProvider) {
                ToggleState state{};
                check(SUCCEEDED(toggleProvider->get_ToggleState(&state)) &&
                      state == ToggleState_Off, 119);
                regressionStartupStatus.state = StartupState::Enabled;
                RefreshStartupControls();
                check(SUCCEEDED(toggleProvider->get_ToggleState(&state)) &&
                      state == ToggleState_On, 120);
                toggleProvider->Release();
            }
            firstChild->Release();
        }
        rootAuto->Release();
        regressionStartupStatus.state = StartupState::Disabled;
        RefreshStartupControls();

        POINT maxPoint{(g.maxButtonRect.left + g.maxButtonRect.right) / 2, Scale(16)};
        ClientToScreen(host, &maxPoint);
        check(HitTest(maxPoint) == HTMAXBUTTON, 110);
        POINT closePoint{(g.closeButtonRect.left + g.closeButtonRect.right) / 2, Scale(16)};
        ClientToScreen(host, &closePoint);
        check(HitTest(closePoint) == HTCLOSE, 111);
        TITLEBARINFOEX titleInfo{};
        titleInfo.cbSize = sizeof(titleInfo);
        SendMessageW(host, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&titleInfo));
        check(titleInfo.rgrect[3].right > titleInfo.rgrect[3].left &&
              !(titleInfo.rgstate[3] & STATE_SYSTEM_INVISIBLE), 112);
        unsigned generation = g.renderer.Generation();
        g.renderer.Discard();
        InvalidateRect(host, nullptr, FALSE);
        UpdateWindow(host);
        check(!g.renderingFailed && g.renderer.Generation() > generation, 113);
        HideSettingsWindow();
        check(std::wstring(kGitHubUrl) == L"https://github.com/s1kSec/windowcue", 75);
        ActivateWindow(host);
    }

    auto submittedTransform = [&]() -> const DWM_THUMBNAIL_PROPERTIES* {
        for (auto entry = submissions.rbegin(); entry != submissions.rend(); ++entry) {
            if (entry->thumbnail == thumbnail &&
                (entry->properties.dwFlags & DWM_TNP_RECTDESTINATION)) {
                return &entry->properties;
            }
        }
        return nullptr;
    };
    auto boundedTransform = [&]() {
        const auto* properties = submittedTransform();
        if (!properties || !(properties->dwFlags & DWM_TNP_RECTSOURCE)) return false;
        const RECT& target = properties->rcDestination;
        const RECT& card = g.windows[1].previewRect;
        SIZE sourceSize{};
        return SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &sourceSize)) &&
            SameRect(properties->rcSource, MakeRect(0, 0, sourceSize.cx, sourceSize.cy)) &&
            target.left >= card.left && target.top >= card.top &&
            target.right <= card.right && target.bottom <= card.bottom;
    };
    check(boundedTransform(), 36);
    size_t unchangedCount = submissions.size();
    UpdateThumbnails();
    check(submissions.size() == unchangedCount, 37);
    UpdateThumbnails(true);
    check(submissions.size() > unchangedCount && boundedTransform(), 38);

    RECT initialDestination = g.windows[1].appliedThumbnailRect;
    size_t beforeResize = submissions.size();
    SetWindowPos(source, nullptr, 0, 0, 1280, 960, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    DwmFlush();
    UpdateThumbnails();
    check(submissions.size() > beforeResize && boundedTransform() &&
          SameRect(initialDestination, g.windows[1].appliedThumbnailRect), 39);
    for (SIZE sourceSize : {SIZE{1400, 240}, SIZE{240, 1400}, SIZE{640, 480}}) {
        SetWindowPos(source, nullptr, 0, 0, sourceSize.cx, sourceSize.cy,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        DwmFlush();
        UpdateThumbnails();
        check(boundedTransform() && g.windows[1].thumbnail == thumbnail, 40);
    }

    HRGN update = CreateRectRgn(0, 0, 0, 0);
    auto previewsUntouched = [&]() {
        GetUpdateRgn(host, update, FALSE);
        return !RectInRegion(update, &g.windows[1].previewRect);
    };
    for (bool dark : {true, false}) {
        g.darkMode = dark;
        ValidateRect(host, nullptr);
        size_t beforeDeactivation = submissions.size();
        SendMessageW(host, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(source));
        check(!g.windowActive && previewsUntouched(), 22);
        check(submissions.size() > beforeDeactivation && boundedTransform(), 41);
        check(PtInRegion(update, g.titleRect.left + 50, g.titleRect.top + 10), 30);
        ValidateRect(host, nullptr);
        SendMessageW(host, WM_ACTIVATE, WA_ACTIVE, reinterpret_cast<LPARAM>(source));
        check(g.windowActive && previewsUntouched(), 23);
        ValidateRect(host, nullptr);
        g.hovered = 1;
        g.themeButtonHovered = true;
        SendMessageW(host, WM_MOUSELEAVE, 0, 0);
        check(g.hovered == -1 && !g.themeButtonHovered && previewsUntouched(), 24);
        check(PtInRegion(update, g.windows[1].cardRect.left + 10,
                         g.windows[1].cardRect.top + 10), 31);
        ValidateRect(host, nullptr);
        RECT card = g.windows[1].cardRect;
        SendMessageW(host, WM_MOUSEMOVE, 0, MAKELPARAM(card.left + 10, card.top + 10));
        check(g.hovered == 1 && previewsUntouched(), 25);
        ValidateRect(host, nullptr);
        SelectWindowCard(1);
        check(g.selected == 1 && previewsUntouched(), 26);
        EndCapture(false);
        check(g.windows[1].thumbnail == thumbnail, 27);
        SIZE size{};
        check(SUCCEEDED(DwmQueryThumbnailSourceSize(thumbnail, &size)) &&
              size.cx > 0 && size.cy > 0, 28);
    }

    ActiveBinding targetBinding;
    targetBinding.hotkeyId = kFirstHotkeyId + 100;
    targetBinding.target = source;
    g.bindings.push_back(targetBinding);
    ActivateWindow(host);
    check(GetForegroundWindow() == host, 32);
    ValidateRect(host, nullptr);
    SendMessageW(host, WM_HOTKEY, targetBinding.hotkeyId, 0);
    UpdateThumbnails();
    check(GetForegroundWindow() == source && previewsUntouched(), 33);
    check(g.windows[1].thumbnail == thumbnail && g.windows[1].thumbnailVisible, 34);
    check(g.windows[1].thumbnailSourceForeground && boundedTransform(), 42);
    ShowWindow(source, SW_MINIMIZE);
    DwmFlush();
    UpdateThumbnails();
    check(g.windows[1].thumbnailSourceIconic && boundedTransform(), 43);
    ActivateWindow(source);
    MSG pending{};
    while (PeekMessageW(&pending, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&pending);
        DispatchMessageW(&pending);
    }
    DwmFlush();
    UpdateThumbnails();
    check(!g.windows[1].thumbnailSourceIconic && boundedTransform(), 44);
    ActivateWindow(host);
    check(GetForegroundWindow() == host && g.windows[1].thumbnail == thumbnail, 35);

    DeleteObject(update);
    DestroyWindow(source);
    DestroyWindow(host);
    if (originalForeground && IsWindow(originalForeground)) ActivateWindow(originalForeground);
    CoUninitialize();
    return failure;
}
