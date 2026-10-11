// Keep regression-only entry points out of the release EXEs. Including the
// production TU lets these checks exercise the actual message handlers.
#define wWinMain WindowCueProductionEntry
#include "../src/Main.cpp"
#undef wWinMain

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
        WS_POPUP | WS_CAPTION, -32000, -32000, 1080, 610,
        nullptr, nullptr, instance, nullptr);
    HWND source = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Preview source",
        WS_OVERLAPPEDWINDOW, -32000, -32000, 640, 480,
        nullptr, nullptr, instance, nullptr);
    if (!host || !source) return 92;
    HWND originalForeground = GetForegroundWindow();

    int failure = 0;
    auto check = [&](bool ok, int code) { if (!ok && !failure) failure = code; };
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
    HRGN update = CreateRectRgn(0, 0, 0, 0);
    auto previewsUntouched = [&]() {
        GetUpdateRgn(host, update, FALSE);
        return !RectInRegion(update, &g.windows[1].previewRect);
    };
    // These are the production paths that previously repainted every preview.
    for (bool dark : {true, false}) {
        g.darkMode = dark;
        ValidateRect(host, nullptr);
        SendMessageW(host, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(source));
        check(!g.windowActive && previewsUntouched(), 22);
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
        SendMessageW(host, WM_MOUSEMOVE, 0,
                     MAKELPARAM(card.left + 10, card.top + 10));
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

    // Exercise the real hotkey dispatch and Win32 focus handoff with live DWM
    // surfaces. Only the two off-screen test windows are used as targets.
    ActiveBinding targetBinding;
    targetBinding.hotkeyId = kFirstHotkeyId + 100;
    targetBinding.target = source;
    g.bindings.push_back(targetBinding);
    ActivateWindow(host);
    check(GetForegroundWindow() == host, 32);
    ValidateRect(host, nullptr);
    SendMessageW(host, WM_HOTKEY, targetBinding.hotkeyId, 0);
    check(GetForegroundWindow() == source && previewsUntouched(), 33);
    check(g.windows[1].thumbnail == thumbnail && g.windows[1].thumbnailVisible, 34);
    ActivateWindow(host);
    check(GetForegroundWindow() == host && g.windows[1].thumbnail == thumbnail, 35);
    DeleteObject(update);
    DestroyWindow(source);
    DestroyWindow(host);
    if (originalForeground && IsWindow(originalForeground)) ActivateWindow(originalForeground);
    CoUninitialize();
    return failure;
}
