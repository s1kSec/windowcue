#include "WindowCueNative.h"
#include "RenderContext.h"
#include "Theme.h"
#include "Automation.h"
#include "WindowChrome.h"
#include "../resource.h"

#include <objbase.h>
#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")

namespace windowcue {
namespace {

constexpr wchar_t kWindowClass[] = L"WindowCue.Native.Window";
constexpr wchar_t kSettingsClass[] = L"WindowCue.Native.Settings";
constexpr wchar_t kGitHubUrl[] = L"https://github.com/s1kSec/windowcue";
constexpr int kStartupButtonId = 301;
constexpr int kGitHubButtonId = 302;
constexpr int kSettingsWidth = 300;
constexpr int kSettingsHeight = 106;
constexpr UINT_PTR kTrayRetryTimer = 3;
constexpr UINT_PTR kCaptionAnimationTimer = 4;
constexpr UINT_PTR kRenderRetryTimer = 5;
constexpr ULONGLONG kCaptionHoverDuration = 125;
constexpr UINT_PTR kSettingsTooltipId = 1;
constexpr UINT_PTR kThemeTooltipId = 2;
constexpr int kNormalWindowWidth = 1080;
constexpr DWORD kMainWindowStyle=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_THICKFRAME;


struct Texts {
    const wchar_t* selectWindow;
    const wchar_t* bind;
    const wchar_t* unbind;
    const wchar_t* openSettings;
    const wchar_t* exit;
    const wchar_t* clickToRecord;
    const wchar_t* useModifier;
    const wchar_t* recordFirst;
    const wchar_t* occupiedSuffix;
    const wchar_t* noWindows;
    const wchar_t* settings;
    const wchar_t* startup;
    const wchar_t* startupWindowsDisabled;
    const wchar_t* startupPolicyDisabled;
    const wchar_t* startupUnavailable;
    const wchar_t* startupEnable;
    const wchar_t* startupDisable;
    const wchar_t* startupWindowsSettings;
    const wchar_t* github;
    const wchar_t* linkError;
    const wchar_t* settingsTooltip;
    const wchar_t* lightModeTooltip;
    const wchar_t* darkModeTooltip;
};

const Texts& T() {
#ifdef LANG_EN
    static const Texts texts{
        L"Select a window first", L"Bind", L"Unbind", L"Open settings", L"Exit",
        L"Click and press a shortcut", L"Use Ctrl / Alt / Shift / Win",
        L"Record a shortcut with a modifier first", L" is already used by another app",
        L"No user windows found", L"Settings", L"Start with Windows",
        L"Disabled in Windows startup settings", L"Disabled by Windows policy",
        L"Startup is unavailable", L"Enable start with Windows", L"Disable start with Windows",
        L"Open Windows startup settings",
        L"GitHub project", L"Could not open the link. Check your default browser.",
        L"Settings (Ctrl+,)", L"Switch to light mode", L"Switch to dark mode"};
#else
    static const Texts texts{
        L"请先选择一个窗口", L"绑定", L"取消绑定", L"打开设置", L"退出",
        L"点击后按快捷键", L"请同时使用 Ctrl / Alt / Shift / Win",
        L"请先录入包含修饰键的快捷键", L" 已被其他程序占用",
        L"没有找到用户窗口", L"设置", L"开机自启动",
        L"已在 Windows 启动设置中禁用", L"已被 Windows 策略禁用",
        L"开机自启动不可用", L"开启开机自启动", L"关闭开机自启动",
        L"打开 Windows 启动设置",
        L"GitHub 项目", L"无法打开链接，请检查默认浏览器设置。",
        L"设置（Ctrl+,）", L"切换到亮色模式", L"切换到暗夜模式"};
#endif
    return texts;
}

struct CaptionHoverAnimation {
    double value{};
    double from{};
    double target{};
    ULONGLONG started{};
};

struct AppState {
    HINSTANCE instance{};
    HWND hwnd{};
    HICON appIcon{};
    HICON appLargeIcon{};
    RenderContext renderer;
    RenderContext settingsRenderer;
    RenderContext startupRenderer, githubRenderer, startupLabelRenderer, statusRenderer;
    int captionHot{}, captionPressed{};
    bool renderingFailed{};
    ChromeStatus chromeStatus,settingsChromeStatus;
    unsigned renderRetries{};
    bool settingsCloseHot{}, settingsClosePressed{};
    HWND settingsWindow{};
    HWND captionTooltips{};
    HWND startupButton{};
    HWND githubButton{};
    HWND startupLabel{};
    HWND startupStatusLabel{};
    HFONT settingsFont{};
    UINT settingsDpi{96};
    UINT taskbarCreatedMessage{};
    int trayRetries{};
    StartupStatus startupStatus;
    std::wstring settingsMessage;
    NOTIFYICONDATAW tray{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    UINT dpi{96};

    std::vector<WindowInfo> windows;
    std::vector<ActiveBinding> bindings;
    int selected{-1};
    int hovered{-1};
    int nextHotkeyId{kFirstHotkeyId};
    int scrollOffset{};
    int maxScroll{};
    bool capturing{};
    bool captured{};
    bool inputError{};
    bool refreshing{};
    bool allowExit{};
    bool trackingMouse{};
    bool windowActive{true};
    bool initialLayout{true};
    bool resizingForContent{};
    bool draggingScrollbar{};
    bool darkMode{true};
    bool themeButtonHovered{};
    bool themeButtonPressed{};
    bool settingsButtonHovered{};
    bool settingsButtonPressed{};
    bool captionAnimationsAllowed{true};
    bool captionAnimationRunning{};
    CaptionHoverAnimation themeHoverAnimation;
    CaptionHoverAnimation settingsHoverAnimation;
    int scrollbarDragY{};
    int scrollbarStartOffset{};
    UINT capturedModifiers{};
    UINT capturedKey{};
    std::wstring inputText;
    std::wstring signature;

    RECT titleRect{};
    RECT previewOuter{};
    RECT previewInner{};
    RECT bindingRect{};
    RECT hotkeyRect{};
    RECT bindButtonRect{};
    RECT themeButtonRect{};
    RECT settingsButtonRect{};
    RECT minButtonRect{};
    RECT maxButtonRect{};
    RECT closeButtonRect{};
    RECT scrollbarRect{};
    RECT scrollbarThumb{};
};

AppState g;

void HideSettingsWindow();
void ShowSettingsWindow();
void LayoutSettingsControls();

std::wstring CurrentExecutablePath() {
    wchar_t path[32768]{};
    DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    return length > 0 && length < std::size(path)
        ? std::wstring(path, static_cast<size_t>(length))
        : std::wstring();
}

WindowInfo CreateWindowCueItem() {
    WindowInfo item;
    item.handle = g.hwnd;
    item.title = L"WindowCue";
    item.className = kWindowClass;
    item.processId = GetCurrentProcessId();
    item.processPath = CurrentExecutablePath();
    item.isWindowCue = true;
    return item;
}

void PrependWindowCueItem(std::vector<WindowInfo>& windows) {
    windows.insert(windows.begin(), CreateWindowCueItem());
}

const ThemePalette& P() { return GetThemePalette(g.darkMode); }

UINT QueryWindowDpi(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using GetWindowDpi = UINT(WINAPI*)(HWND);
    auto getWindowDpi = reinterpret_cast<GetWindowDpi>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (getWindowDpi) return getWindowDpi(hwnd);
    HDC dc = GetDC(hwnd);
    UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96U;
    if (dc) ReleaseDC(hwnd, dc);
    return dpi ? dpi : 96U;
}

UINT QuerySystemDpi() {
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96U;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96U;
}

int Scale(int value) {
    return MulDiv(value, static_cast<int>(g.dpi), 96);
}

RECT MakeRect(int left, int top, int right, int bottom) {
    return RECT{left, top, right, bottom};
}

int RectWidth(const RECT& rect) { return rect.right - rect.left; }
int RectHeight(const RECT& rect) { return rect.bottom - rect.top; }

bool PointInside(const RECT& rect, POINT point) {
    return PtInRect(&rect, point) != FALSE;
}

void PopCaptionTooltip() {
    if (g.captionTooltips) SendMessageW(g.captionTooltips, TTM_POP, 0, 0);
}

void UpdateCaptionTooltipText() {
    if (!g.captionTooltips) return;
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = g.hwnd;
    tool.uId = kThemeTooltipId;
    tool.lpszText = const_cast<wchar_t*>(g.darkMode ? T().lightModeTooltip : T().darkModeTooltip);
    SendMessageW(g.captionTooltips, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void UpdateCaptionTooltipRect(UINT_PTR id, const RECT& rect) {
    if (!g.captionTooltips) return;
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = g.hwnd;
    tool.uId = id;
    tool.rect = rect;
    SendMessageW(g.captionTooltips, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
}

void CreateCaptionTooltips() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) return;
    g.captionTooltips = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g.hwnd, nullptr, g.instance, nullptr);
    if (!g.captionTooltips) return;
    SetWindowPos(g.captionTooltips, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(g.captionTooltips, TTM_SETDELAYTIME, TTDT_INITIAL, 500);
    SendMessageW(g.captionTooltips, TTM_SETDELAYTIME, TTDT_RESHOW, 100);
    SendMessageW(g.captionTooltips, TTM_SETDELAYTIME, TTDT_AUTOPOP, 5000);
    for (UINT_PTR id : {kSettingsTooltipId, kThemeTooltipId}) {
        TOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
        tool.hwnd = g.hwnd;
        tool.uId = id;
        tool.rect = id == kSettingsTooltipId ? g.settingsButtonRect : g.themeButtonRect;
        tool.lpszText = const_cast<wchar_t*>(id == kSettingsTooltipId ? T().settingsTooltip :
            (g.darkMode ? T().lightModeTooltip : T().darkModeTooltip));
        SendMessageW(g.captionTooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

bool AdvanceCaptionHover(CaptionHoverAnimation& animation, ULONGLONG now) {
    if (animation.value == animation.target) return false;
    double previous = animation.value;
    double progress = std::min(1.0, static_cast<double>(now - animation.started) /
                                   static_cast<double>(kCaptionHoverDuration));
    double remaining = 1.0 - progress;
    animation.value = progress >= 1.0 ? animation.target :
        animation.from + (animation.target - animation.from) *
        (1.0 - remaining * remaining * remaining);
    return animation.value != previous;
}

void SyncCaptionAnimationTimer() {
    bool active = g.captionAnimationsAllowed && IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd) &&
        (g.themeHoverAnimation.value != g.themeHoverAnimation.target ||
         g.settingsHoverAnimation.value != g.settingsHoverAnimation.target);
    if (active && !g.captionAnimationRunning) {
        g.captionAnimationRunning = SetTimer(g.hwnd, kCaptionAnimationTimer, 16, nullptr) != 0;
        if (!g.captionAnimationRunning) {
            g.themeHoverAnimation.value = g.themeHoverAnimation.target;
            g.settingsHoverAnimation.value = g.settingsHoverAnimation.target;
        }
    } else if (!active && g.captionAnimationRunning) {
        KillTimer(g.hwnd, kCaptionAnimationTimer);
        g.captionAnimationRunning = false;
    }
}

void SetCaptionHovered(bool settings, bool hovered) {
    bool& state = settings ? g.settingsButtonHovered : g.themeButtonHovered;
    if (state == hovered) return;
    state = hovered;
    CaptionHoverAnimation& animation = settings ? g.settingsHoverAnimation : g.themeHoverAnimation;
    ULONGLONG now = GetTickCount64();
    AdvanceCaptionHover(animation, now);
    animation.from = animation.value;
    animation.target = hovered ? 1.0 : 0.0;
    animation.started = now;
    if (!g.captionAnimationsAllowed || !IsWindowVisible(g.hwnd) || IsIconic(g.hwnd)) {
        animation.value = animation.target;
    }
    SyncCaptionAnimationTimer();
    InvalidateRect(g.hwnd, settings ? &g.settingsButtonRect : &g.themeButtonRect, FALSE);
}

void RefreshCaptionAnimationPolicy() {
    BOOL enabled = TRUE;
    g.captionAnimationsAllowed = !SystemParametersInfoW(
        SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled;
    if (!g.captionAnimationsAllowed) {
        g.settingsHoverAnimation.value = g.settingsHoverAnimation.target;
        g.themeHoverAnimation.value = g.themeHoverAnimation.target;
        InvalidateRect(g.hwnd, &g.settingsButtonRect, FALSE);
        InvalidateRect(g.hwnd, &g.themeButtonRect, FALSE);
    }
    SyncCaptionAnimationTimer();
}

void ResetCaptionInteraction() {
    KillTimer(g.hwnd, kCaptionAnimationTimer);
    g.captionAnimationRunning = false;
    if (g.trackingMouse) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_CANCEL, g.hwnd, 0};
        TrackMouseEvent(&tracking);
        g.trackingMouse = false;
    }
    g.settingsButtonHovered = g.themeButtonHovered = false;
    g.settingsButtonPressed = g.themeButtonPressed = false;
    g.captionHot=g.captionPressed=0;
    g.settingsHoverAnimation = {};
    g.themeHoverAnimation = {};
    PopCaptionTooltip();
}

void DeleteFonts() {
    if (g.titleFont) DeleteObject(g.titleFont);
    if (g.bodyFont) DeleteObject(g.bodyFont);
    if (g.smallFont) DeleteObject(g.smallFont);
    g.titleFont = g.bodyFont = g.smallFont = nullptr;
}

HFONT CreateUiFont(int pointSize, int weight) {
    int logicalHeight = -MulDiv(pointSize, static_cast<int>(g.dpi), 72);
    return CreateFontW(logicalHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_NATURAL_QUALITY, VARIABLE_PITCH | FF_SWISS,
                       L"Microsoft YaHei UI");
}

void CreateFonts() {
    DeleteFonts();
    g.titleFont = CreateUiFont(9, FW_MEDIUM);
    g.bodyFont = CreateUiFont(9, FW_NORMAL);
    g.smallFont = CreateUiFont(8, FW_NORMAL);
}

bool UseSoftwareRenderer() {
    wchar_t value[2]{};
    return GetEnvironmentVariableW(L"WINDOWCUE_RENDER_SOFTWARE",value,2)==1 && value[0]==L'1';
}
void FillRounded(RenderContext& dc, const RECT& rect, int diameter, COLORREF color) {
    dc.Rounded(rect, diameter / 2.f, color);
}
void StrokeRounded(RenderContext& dc, const RECT& rect, int diameter, int width, COLORREF color) {
    dc.Stroke(rect, diameter / 2.f, static_cast<float>(width), color);
}
void DrawTextValue(RenderContext& dc, const std::wstring& text, RECT rect, HFONT font,
                   COLORREF color, UINT format) { dc.Text(text,rect,font,color,format); }
void DeleteThemeIcons() { g.renderer.Discard(); }
void RefreshThemeIcons() { g.renderer.Discard(); }
COLORREF BlendCaptionColor(COLORREF first, COLORREF second, double amount) {
    auto channel=[amount](BYTE a,BYTE b) { return static_cast<BYTE>(a+(b-a)*amount+.5); };
    return RGB(channel(GetRValue(first),GetRValue(second)),channel(GetGValue(first),GetGValue(second)),
               channel(GetBValue(first),GetBValue(second)));
}
void DrawCaptionButtonBackground(RenderContext& dc, const RECT& rect, double hover,
                                 bool pressed, bool selected) {
    if(hover<=0 && !pressed && !selected) return;
    RECT background=rect;InflateRect(&background,-Scale(3),-Scale(3));
    dc.Rounded(background,static_cast<float>(Scale(3)),pressed ? P().themePressed : P().themeHover,
               pressed || selected ? 1.f : static_cast<float>(hover));
}
void DrawCaptionButtons(RenderContext& dc) {
    bool settingsPressed=g.settingsButtonPressed && g.settingsButtonHovered;
    DrawCaptionButtonBackground(dc,g.settingsButtonRect,g.settingsHoverAnimation.value,
        settingsPressed,g.settingsWindow && IsWindowVisible(g.settingsWindow));
    dc.Icon(VectorIcon::Settings,g.settingsButtonRect,P().mainText,18,settingsPressed ? 1.f : 0.f);
    DrawCaptionButtonBackground(dc,g.themeButtonRect,g.themeHoverAnimation.value,
        g.themeButtonPressed && g.themeButtonHovered,false);
    dc.Icon(g.darkMode ? VectorIcon::Sun : VectorIcon::Moon,g.themeButtonRect,P().mainText,
            18,g.themeButtonPressed && g.themeButtonHovered ? 1.f : 0.f);
    int id=1;
    for(const RECT& r : {g.minButtonRect,g.maxButtonRect,g.closeButtonRect}) {
        if(g.captionHot==id || g.captionPressed==id)
            dc.Fill(r,id==3 ? RGB(196,43,44) : P().themeHover);
        dc.Icon(id==1 ? VectorIcon::Minimize : id==2 ? (IsZoomed(g.hwnd) ? VectorIcon::Restore :
            VectorIcon::Maximize) : VectorIcon::Close,r,
            id==3 && (g.captionHot==3 || g.captionPressed==3) ? RGB(255,255,255) : P().mainText,16);
        ++id;
    }
}

int BindingIndexForWindow(HWND hwnd) {
    for (size_t i = 0; i < g.bindings.size(); ++i) {
        if (g.bindings[i].target == hwnd) return static_cast<int>(i);
    }
    return -1;
}

void UpdateBoundFlags() {
    for (WindowInfo& item : g.windows) item.bound = BindingIndexForWindow(item.handle) >= 0;
}

std::wstring CreateSignature(const std::vector<WindowInfo>& windows) {
    std::vector<std::wstring> identities;
    identities.reserve(windows.size());
    for (const WindowInfo& item : windows) {
        wchar_t identity[96]{};
        swprintf_s(identity, L"%p:%lu:", item.handle,
                   static_cast<unsigned long>(item.processId));
        identities.emplace_back(identity + item.className);
    }
    std::sort(identities.begin(), identities.end());

    std::wstring signature;
    for (const std::wstring& identity : identities) signature += identity + L"\n";
    return signature;
}

void UnregisterAllBindings() {
    for (ActiveBinding& binding : g.bindings) {
        if (binding.registered) {
            UnregisterHotKey(g.hwnd, binding.hotkeyId);
            binding.registered = false;
        }
    }
}

void SaveBindings() {
    SaveSettings(g.bindings);
}

void RestoreBindings() {
    bool changed = false;
    for (size_t i = 0; i < g.bindings.size();) {
        ActiveBinding& binding = g.bindings[i];
        if (!binding.target || !IsWindow(binding.target)) {
            if (binding.registered) UnregisterHotKey(g.hwnd, binding.hotkeyId);
            g.bindings.erase(g.bindings.begin() + static_cast<ptrdiff_t>(i));
            changed = true;
            continue;
        }
        if (!binding.registered) {
            binding.registered = RegisterHotKey(
                g.hwnd, binding.hotkeyId,
                binding.settings.modifiers | MOD_NOREPEAT,
                binding.settings.virtualKey) != FALSE;
            // Another WindowCue branch or application may temporarily own the
            // shortcut. Preserve the setting and retry later; only a destroyed
            // target HWND makes a binding stale.
        }
        ++i;
    }
    if (changed) SaveBindings();
    UpdateBoundFlags();
}

void EndCapture(bool restore) {
    if (!g.capturing) return;
    g.capturing = false;
    if (restore) RestoreBindings();
}

// The HWND renderer explicitly imports this nonrectangular update region.
// Decoration and focus paints leave the live DWM preview area untouched.
void InvalidateCardDecoration(int index) {
    if (index < 0 || index >= static_cast<int>(g.windows.size())) return;
    const WindowInfo& item = g.windows[index];
    HRGN decoration = CreateRectRgnIndirect(&item.cardRect);
    if (!item.isWindowCue) {
        HRGN preview = CreateRectRgnIndirect(&item.previewRect);
        CombineRgn(decoration, decoration, preview, RGN_DIFF);
        DeleteObject(preview);
    }
    InvalidateRgn(g.hwnd, decoration, FALSE);
    DeleteObject(decoration);
}

void InvalidateWindowChrome() {
    InvalidateRect(g.hwnd, &g.titleRect, FALSE);
    if (IsZoomed(g.hwnd)) return;
    RECT client{};
    GetClientRect(g.hwnd, &client);
    const RECT edges[] = {
        {0, 0, 1, client.bottom},
        {client.right - 1, 0, client.right, client.bottom},
        {0, client.bottom - 1, client.right, client.bottom}
    };
    for (const RECT& edge : edges) InvalidateRect(g.hwnd, &edge, FALSE);
}

void BeginCapture() {
    UnregisterAllBindings();
    g.capturing = true;
    g.captured = false;
    g.capturedModifiers = 0;
    g.capturedKey = 0;
    g.inputText.clear();
    g.inputError = false;
    SetFocus(g.hwnd);
    InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
}

void ResetSelection() {
    EndCapture(true);
    g.selected = -1;
    g.captured = false;
    g.inputText.clear();
    g.inputError = false;
}

void SelectWindowCard(int index) {
    if (index < 0 || index >= static_cast<int>(g.windows.size())) return;
    if (g.capturing) EndCapture(true);
    int previous = g.selected;
    g.selected = index;
    g.inputError = false;
    int bindingIndex = BindingIndexForWindow(g.windows[index].handle);
    if (bindingIndex >= 0) {
        const ActiveBinding& binding = g.bindings[bindingIndex];
        g.captured = true;
        g.capturedModifiers = binding.settings.modifiers;
        g.capturedKey = binding.settings.virtualKey;
        g.inputText = binding.settings.hotkeyText;
    } else {
        BeginCapture();
    }
    InvalidateCardDecoration(previous);
    InvalidateCardDecoration(index);
    InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
}

void RemoveBinding(size_t index) {
    if (index >= g.bindings.size()) return;
    if (g.bindings[index].registered) {
        UnregisterHotKey(g.hwnd, g.bindings[index].hotkeyId);
    }
    if (g.bindings[index].settings.isWindowCue) MarkSelfHotkeyInitialized();
    g.bindings.erase(g.bindings.begin() + static_cast<ptrdiff_t>(index));
    SaveBindings();
    UpdateBoundFlags();
}

void BindOrUnbindSelected() {
    if (g.selected < 0 || g.selected >= static_cast<int>(g.windows.size())) return;
    HWND target = g.windows[g.selected].handle;
    int existing = BindingIndexForWindow(target);
    if (existing >= 0) {
        RemoveBinding(static_cast<size_t>(existing));
        BeginCapture();
        InvalidateRect(g.hwnd, nullptr, FALSE);
        return;
    }

    if (!g.captured || !g.capturedModifiers || !g.capturedKey) {
        g.inputText = T().recordFirst;
        g.inputError = true;
        InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
        return;
    }
    for (const ActiveBinding& binding : g.bindings) {
        if (binding.settings.modifiers == g.capturedModifiers &&
            binding.settings.virtualKey == g.capturedKey) {
            g.inputText = BuildHotkeyText(g.capturedModifiers, g.capturedKey) + T().occupiedSuffix;
            g.inputError = true;
            InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
            return;
        }
    }

    g.capturing = false;
    RestoreBindings();
    int hotkeyId = g.nextHotkeyId++;
    if (!RegisterHotKey(g.hwnd, hotkeyId, g.capturedModifiers | MOD_NOREPEAT, g.capturedKey)) {
        g.inputText = BuildHotkeyText(g.capturedModifiers, g.capturedKey) + T().occupiedSuffix;
        g.inputError = true;
        InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
        return;
    }

    ActiveBinding binding;
    binding.hotkeyId = hotkeyId;
    binding.target = target;
    binding.registered = true;
    binding.settings.processPath = g.windows[g.selected].processPath;
    binding.settings.windowTitle = g.windows[g.selected].title;
    binding.settings.className = g.windows[g.selected].className;
    binding.settings.windowHandle = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(target));
    binding.settings.isWindowCue = g.windows[g.selected].isWindowCue;
    binding.settings.modifiers = g.capturedModifiers;
    binding.settings.virtualKey = g.capturedKey;
    binding.settings.hotkeyText = BuildHotkeyText(g.capturedModifiers, g.capturedKey);
    g.inputText = binding.settings.hotkeyText;
    g.bindings.push_back(std::move(binding));
    SaveBindings();
    if (g.windows[g.selected].isWindowCue) MarkSelfHotkeyInitialized();
    UpdateBoundFlags();
    g.inputError = false;
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void LoadBindings() {
    std::vector<BindingSettings> settings = LoadSettings();
    for (BindingSettings& item : settings) {
        HWND target = item.isWindowCue ? g.hwnd :
            FindBestMatch(item, GetCurrentProcessId());
        if (!target || BindingIndexForWindow(target) >= 0) continue;
        ActiveBinding binding;
        binding.hotkeyId = g.nextHotkeyId++;
        if (item.hotkeyText.empty()) {
            item.hotkeyText = BuildHotkeyText(item.modifiers, item.virtualKey);
        }
        item.windowHandle = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(target));
        binding.settings = std::move(item);
        binding.target = target;
        binding.registered = RegisterHotKey(
            g.hwnd, binding.hotkeyId,
            binding.settings.modifiers | MOD_NOREPEAT,
            binding.settings.virtualKey) != FALSE;
        g.bindings.push_back(std::move(binding));
    }
    // Initialize once so cancelling the default remains cancelled after restart.
    // A saved self binding (including a custom chord) takes precedence.
    bool hasSelfBinding = BindingIndexForWindow(g.hwnd) >= 0;
    bool initializeSelf = !hasSelfBinding && !IsSelfHotkeyInitialized();
    if (initializeSelf) {
        bool chordUsed = std::any_of(g.bindings.begin(), g.bindings.end(),
            [](const ActiveBinding& binding) {
                return binding.settings.modifiers == MOD_ALT &&
                       binding.settings.virtualKey == 'P';
            });
        if (!chordUsed) {
            WindowInfo self = CreateWindowCueItem();
            ActiveBinding binding;
            binding.hotkeyId = g.nextHotkeyId++;
            binding.target = g.hwnd;
            binding.settings.processPath = self.processPath;
            binding.settings.windowTitle = self.title;
            binding.settings.className = self.className;
            binding.settings.windowHandle = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(g.hwnd));
            binding.settings.isWindowCue = true;
            binding.settings.modifiers = MOD_ALT;
            binding.settings.virtualKey = 'P';
            binding.settings.hotkeyText = BuildHotkeyText(MOD_ALT, 'P');
            binding.registered = RegisterHotKey(
                g.hwnd, binding.hotkeyId, MOD_ALT | MOD_NOREPEAT, 'P') != FALSE;
            g.bindings.push_back(std::move(binding));
            hasSelfBinding = true;
        }
    }
    if (SaveSettings(g.bindings) && hasSelfBinding) MarkSelfHotkeyInitialized();
}

void RegisterThumbnails() {
    for (WindowInfo& item : g.windows) {
        if (item.isWindowCue || item.thumbnail) continue;
        if (SUCCEEDED(DwmRegisterThumbnail(g.hwnd, item.handle, &item.thumbnail))) {
            item.thumbnailPropertiesValid = false;
            item.thumbnailVisible = false;
        }
    }
}

struct GridLayoutMetrics {
    int titleHeight{Scale(34)};
    int margin{Scale(12)};
    int bindingHeight{Scale(70)};
    int previewTopGap{Scale(7)};
    int bindingGap{Scale(8)};
    int cardWidth{Scale(194)};
    int cardHeight{Scale(146)};
    int cardGap{Scale(8)};
    int previewInset{Scale(7)};
    int cardInset{Scale(2)};
    int scrollbarSpace{Scale(14)};

    int Columns(int clientWidth) const {
        int availableWidth = std::max(cardWidth,
            clientWidth - margin * 2 - previewInset * 2 - scrollbarSpace);
        return std::max(1, (availableWidth + cardGap) / (cardWidth + cardGap));
    }

    int ContentHeight(size_t count, int clientWidth) const {
        if (!count) return 0;
        int columns = Columns(clientWidth);
        int rows = (static_cast<int>(count) + columns - 1) / columns;
        return rows * cardHeight + (rows - 1) * cardGap + cardInset * 2;
    }

    int ClientHeight(size_t count, int clientWidth) const {
        return titleHeight + previewTopGap + previewInset * 2 + bindingGap +
               bindingHeight + margin + ContentHeight(count, clientWidth);
    }
};

void Layout() {
    if (!g.hwnd) return;
    RECT previousThemeRect = g.themeButtonRect;
    RECT previousSettingsRect = g.settingsButtonRect;
    RECT client{};
    GetClientRect(g.hwnd, &client);
    GridLayoutMetrics metrics;
    int titleHeight = metrics.titleHeight;
    int margin = metrics.margin;
    int bindingHeight = metrics.bindingHeight;
    int cardWidth = metrics.cardWidth;
    int cardHeight = metrics.cardHeight;
    int cardGap = metrics.cardGap;
    int previewInset = metrics.previewInset;
    int captionWidth = Scale(46);

    g.titleRect = MakeRect(0, 0, client.right, titleHeight);
    g.closeButtonRect = MakeRect(client.right - captionWidth, 0, client.right, titleHeight);
    g.maxButtonRect = MakeRect(client.right - captionWidth * 2, 0,
                               client.right - captionWidth, titleHeight);
    g.minButtonRect = MakeRect(client.right - captionWidth * 3, 0,
                               client.right - captionWidth * 2, titleHeight);

    g.themeButtonRect = MakeRect(g.minButtonRect.left - Scale(38), 0,
                                 g.minButtonRect.left, titleHeight);
    g.settingsButtonRect = MakeRect(g.themeButtonRect.left - Scale(38), 0,
                                    g.themeButtonRect.left, titleHeight);
    if (!EqualRect(&previousThemeRect, &g.themeButtonRect)) {
        UpdateCaptionTooltipRect(kThemeTooltipId, g.themeButtonRect);
    }
    if (!EqualRect(&previousSettingsRect, &g.settingsButtonRect)) {
        UpdateCaptionTooltipRect(kSettingsTooltipId, g.settingsButtonRect);
    }
    g.bindingRect = MakeRect(margin, client.bottom - margin - bindingHeight,
                             client.right - margin, client.bottom - margin);
    g.previewOuter = MakeRect(margin, titleHeight + metrics.previewTopGap,
                              client.right - margin, g.bindingRect.top - metrics.bindingGap);
    g.previewInner = g.previewOuter;
    InflateRect(&g.previewInner, -previewInset, -previewInset);

    int buttonWidth = Scale(66);
    int inputWidth = Scale(260);
    int controlHeight = Scale(38);
    int controlTop = g.bindingRect.top + (RectHeight(g.bindingRect) - controlHeight) / 2;
    g.bindButtonRect = MakeRect(g.bindingRect.right - Scale(15) - buttonWidth, controlTop,
                                g.bindingRect.right - Scale(15), controlTop + controlHeight);
    g.hotkeyRect = MakeRect(g.bindButtonRect.left - Scale(10) - inputWidth, controlTop,
                            g.bindButtonRect.left - Scale(10), controlTop + controlHeight);

    int columns = metrics.Columns(RectWidth(client));
    int contentHeight = metrics.ContentHeight(g.windows.size(), RectWidth(client));
    g.maxScroll = std::max(0, contentHeight - RectHeight(g.previewInner));
    g.scrollOffset = std::clamp(g.scrollOffset, 0, g.maxScroll);

    int startX = g.previewInner.left + metrics.cardInset;
    int startY = g.previewInner.top + metrics.cardInset - g.scrollOffset;
    for (size_t i = 0; i < g.windows.size(); ++i) {
        int row = static_cast<int>(i) / columns;
        int column = static_cast<int>(i) % columns;
        int left = startX + column * (cardWidth + cardGap);
        int top = startY + row * (cardHeight + cardGap);
        WindowInfo& item = g.windows[i];
        item.cardRect = MakeRect(left, top, left + cardWidth, top + cardHeight);
        item.previewRect = MakeRect(left + Scale(6), top + Scale(40),
                                    left + cardWidth - Scale(6), top + cardHeight - Scale(6));
    }

    g.scrollbarRect = MakeRect(g.previewInner.right - Scale(8), g.previewInner.top,
                                g.previewInner.right, g.previewInner.bottom);
    g.scrollbarThumb = {};
    if (g.maxScroll > 0) {
        int trackHeight = RectHeight(g.scrollbarRect);
        int thumbHeight = std::max(Scale(28),
            trackHeight * trackHeight / (trackHeight + g.maxScroll));
        int travel = trackHeight - thumbHeight;
        int top = g.scrollbarRect.top +
            (g.maxScroll ? travel * g.scrollOffset / g.maxScroll : 0);
        g.scrollbarThumb = MakeRect(g.scrollbarRect.left + Scale(2), top,
                                    g.scrollbarRect.right - Scale(1), top + thumbHeight);
    }
}

void ResizeForContent() {
    if (!g.hwnd || IsZoomed(g.hwnd) || IsIconic(g.hwnd) || g.resizingForContent) return;
    RECT window{};
    RECT client{};
    GetWindowRect(g.hwnd, &window);
    GetClientRect(g.hwnd, &client);
    int width = RectWidth(window);
    GridLayoutMetrics metrics;
    // Use the same client width and individually scaled dimensions as Layout.
    // Measuring the outer width or scaling the summed row height can choose a
    // different column count and leave a clipped row despite available space.
    int frameHeight = std::max(0, RectHeight(window) - RectHeight(client));
    int desired = std::max(Scale(310),
        metrics.ClientHeight(g.windows.size(), RectWidth(client)) + frameHeight);
    LONG top = window.top;

    HMONITOR monitor = MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (GetMonitorInfoW(monitor, &monitorInfo)) {
        desired = std::min(desired, RectHeight(monitorInfo.rcWork));
        top = std::clamp(top, monitorInfo.rcWork.top, monitorInfo.rcWork.bottom - desired);
    }
    if (g.initialLayout || RectHeight(window) != desired || top != window.top) {
        g.resizingForContent = true;
        SetWindowPos(g.hwnd, nullptr, window.left, top, width, desired,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g.resizingForContent = false;
    }
    g.initialLayout = false;
}

void RefreshWindows(bool force) {
    if (g.refreshing) return;
    g.refreshing = true;
    std::vector<WindowInfo> scan = EnumerateWindows(false, GetCurrentProcessId());
    PrependWindowCueItem(scan);
    std::wstring signature = CreateSignature(scan);
    if (!force && signature == g.signature) {
        // Titles may change frequently without changing window identity. Update
        // only their text regions and preserve every registered DWM thumbnail.
        for (WindowInfo& current : g.windows) {
            auto match = std::find_if(scan.begin(), scan.end(), [&](const WindowInfo& item) {
                return item.handle == current.handle;
            });
            if (match == scan.end()) continue;
            bool textChanged = current.title != match->title ||
                               current.processPath != match->processPath;
            current.title = match->title;
            current.className = match->className;
            current.processId = match->processId;
            current.processPath = match->processPath;
            if (textChanged) {
                RECT header = current.cardRect;
                header.bottom = current.previewRect.top;
                InvalidateRect(g.hwnd, &header, FALSE);
                if (g.selected >= 0 &&
                    g.selected < static_cast<int>(g.windows.size()) &&
                    g.windows[g.selected].handle == current.handle) {
                    InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
                }
            }
        }
        ReleaseWindows(scan);
        UpdateBoundFlags();
        g.refreshing = false;
        return;
    }
    ReleaseWindows(scan);

    HWND selectedHandle = (g.selected >= 0 && g.selected < static_cast<int>(g.windows.size()))
        ? g.windows[g.selected].handle : nullptr;

    // Preserve icon and thumbnail ownership for HWNDs that survived the scan.
    // Only truly new windows register a new DWM thumbnail; only removed windows
    // are unregistered by ReleaseWindows below.
    std::vector<WindowInfo> refreshed = EnumerateWindows(true, GetCurrentProcessId());
    PrependWindowCueItem(refreshed);
    for (WindowInfo& item : refreshed) {
        auto existing = std::find_if(g.windows.begin(), g.windows.end(), [&](WindowInfo& old) {
            return old.handle == item.handle;
        });
        if (existing == g.windows.end()) continue;
        if (item.icon) DestroyIcon(item.icon);
        item.icon = existing->icon;
        existing->icon = nullptr;
        item.thumbnail = existing->thumbnail;
        existing->thumbnail = nullptr;
        item.appliedThumbnailRect = existing->appliedThumbnailRect;
        item.appliedThumbnailSourceRect = existing->appliedThumbnailSourceRect;
        item.thumbnailPropertiesValid = existing->thumbnailPropertiesValid;
        item.thumbnailVisible = existing->thumbnailVisible;
        item.thumbnailSourceIconic = existing->thumbnailSourceIconic;
        item.thumbnailSourceForeground = existing->thumbnailSourceForeground;
    }
    g.renderer.ForgetImages();
    ReleaseWindows(g.windows);
    g.windows = std::move(refreshed);
    g.signature = CreateSignature(g.windows);
    g.selected = -1;
    for (size_t i = 0; i < g.windows.size(); ++i) {
        if (g.windows[i].handle == selectedHandle) g.selected = static_cast<int>(i);
    }
    if (selectedHandle && g.selected < 0) ResetSelection();
    UpdateBoundFlags();
    RegisterThumbnails();
    ResizeForContent();
    Layout();
    InvalidateRect(g.hwnd, nullptr, FALSE);
    g.refreshing = false;
}

void ValidateBindings() {
    bool changed = false;
    for (size_t i = 0; i < g.bindings.size();) {
        if (g.bindings[i].target && IsWindow(g.bindings[i].target)) {
            ++i;
            continue;
        }
        if (g.bindings[i].registered) UnregisterHotKey(g.hwnd, g.bindings[i].hotkeyId);
        g.bindings.erase(g.bindings.begin() + static_cast<ptrdiff_t>(i));
        changed = true;
    }
    if (changed) {
        SaveBindings();
        UpdateBoundFlags();
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
}

bool SameRect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
}

void SetThumbnailVisible(WindowInfo& item, bool visible) {
    if (!item.thumbnail) return;
    if (item.thumbnailPropertiesValid && item.thumbnailVisible == visible) return;
    DWM_THUMBNAIL_PROPERTIES properties{};
    properties.dwFlags = DWM_TNP_VISIBLE;
    properties.fVisible = visible;
    if (SUCCEEDED(DwmUpdateThumbnailProperties(item.thumbnail, &properties))) {
        item.thumbnailPropertiesValid = true;
        item.thumbnailVisible = visible;
    }
}

void UpdateThumbnails(bool force = false) {
    if (!IsWindowVisible(g.hwnd) || IsIconic(g.hwnd)) {
        for (WindowInfo& item : g.windows) SetThumbnailVisible(item, false);
        return;
    }
    Layout();
    HWND foreground = GetForegroundWindow();
    for (WindowInfo& item : g.windows) {
        RECT destination = item.previewRect;
        bool fullyVisible = destination.left >= g.previewInner.left &&
                            destination.top >= g.previewInner.top &&
                            destination.right <= g.previewInner.right - Scale(10) &&
                            destination.bottom <= g.previewInner.bottom;
        if (!fullyVisible || !item.thumbnail) {
            SetThumbnailVisible(item, false);
            continue;
        }
        SIZE source{};
        if (FAILED(DwmQueryThumbnailSourceSize(item.thumbnail, &source)) ||
            source.cx <= 0 || source.cy <= 0) {
            SetThumbnailVisible(item, false);
            continue;
        }
        int hostWidth = RectWidth(destination);
        int hostHeight = RectHeight(destination);
        double scale = std::min(static_cast<double>(hostWidth) / source.cx,
                                static_cast<double>(hostHeight) / source.cy);
        int width = std::max(1, static_cast<int>(source.cx * scale + 0.5));
        int height = std::max(1, static_cast<int>(source.cy * scale + 0.5));
        int left = destination.left + (hostWidth - width) / 2;
        int top = destination.top + (hostHeight - height) / 2;
        DWM_THUMBNAIL_PROPERTIES properties{};
        properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY |
                             DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination = MakeRect(left, top, left + width, top + height);
        // Pin both ends of the transform. A source can replace or resize its
        // surface on activation/restore without changing the fitted rectangle.
        properties.rcSource = MakeRect(0, 0, source.cx, source.cy);
        properties.opacity = 255;
        properties.fVisible = TRUE;
        properties.fSourceClientAreaOnly = FALSE;
        bool sourceIconic = IsIconic(item.handle) != FALSE;
        bool sourceForeground = item.handle == foreground;
        if (!force && item.thumbnailPropertiesValid && item.thumbnailVisible &&
            SameRect(item.appliedThumbnailRect, properties.rcDestination) &&
            SameRect(item.appliedThumbnailSourceRect, properties.rcSource) &&
            item.thumbnailSourceIconic == sourceIconic &&
            item.thumbnailSourceForeground == sourceForeground) {
            continue;
        }
        if (SUCCEEDED(DwmUpdateThumbnailProperties(item.thumbnail, &properties))) {
            item.appliedThumbnailRect = properties.rcDestination;
            item.appliedThumbnailSourceRect = properties.rcSource;
            item.thumbnailPropertiesValid = true;
            item.thumbnailVisible = true;
            item.thumbnailSourceIconic = sourceIconic;
            item.thumbnailSourceForeground = sourceForeground;
        }
    }
}

void DrawInterface(HDC, const RECT& updateRect, const std::vector<RECT>& region = {}) {
    RECT client{};GetClientRect(g.hwnd,&client);
    auto& dc=g.renderer;
    if(!dc.Begin(g.hwnd,g.dpi,updateRect,UseSoftwareRenderer())) {
        g.renderingFailed=true;
        if(g.renderRetries<3) SetTimer(g.hwnd,kRenderRetryTimer,150,nullptr);
        return;
    }
    bool clipped=dc.PushRegion(region);
    dc.Fill(client,P().windowBackground);
    int iconSize=Scale(18);
    dc.AppIcon(g.appIcon,MakeRect(Scale(10),(Scale(34)-iconSize)/2,Scale(10)+iconSize,(Scale(34)+iconSize)/2));
    RECT appTitle=MakeRect(Scale(36),0,g.settingsButtonRect.left-Scale(4),Scale(34));
    DrawTextValue(dc,L"WindowCue",appTitle,g.bodyFont,P().titleText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    DrawCaptionButtons(dc);
    // Hover animation stays within the caption. A newly recreated target still
    // needs the complete scene even when its first update was only the caption.
    if(dc.FreshFrame() || updateRect.bottom>g.titleRect.bottom) {
    FillRounded(dc, g.previewOuter, Scale(12), P().previewBackground);
    StrokeRounded(dc, g.previewOuter, Scale(12), Scale(1), P().previewBorder);
    if (g.windows.empty()) {
        DrawTextValue(dc, T().noWindows, g.previewInner, g.bodyFont, P().mutedText,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    dc.PushClip(g.previewInner);
    for (size_t i = 0; i < g.windows.size(); ++i) {
        const WindowInfo& item = g.windows[i];
        RECT intersection{};
        if (!IntersectRect(&intersection, &item.cardRect, &g.previewInner)) continue;
        FillRounded(dc, item.cardRect, Scale(10), P().cardBackground);
        COLORREF border = item.bound ? P().boundBorder :
            (static_cast<int>(i) == g.selected ? P().selectedBorder :
             (static_cast<int>(i) == g.hovered ? P().hoverBorder : P().normalBorder));
        int borderWidth = (item.bound || static_cast<int>(i) == g.selected) ? Scale(2) : Scale(1);
        StrokeRounded(dc, item.cardRect, Scale(10), borderWidth, border);
        if (!item.isWindowCue) {
            FillRounded(dc, item.previewRect, Scale(6), P().previewCanvas);
        }

        int itemIcon = Scale(18);
        HICON displayIcon = item.isWindowCue ? g.appIcon : item.icon;
        if (displayIcon) dc.AppIcon(displayIcon,MakeRect(item.cardRect.left+Scale(9),item.cardRect.top+Scale(7),
            item.cardRect.left+Scale(9)+itemIcon,item.cardRect.top+Scale(7)+itemIcon));
        RECT title = MakeRect(item.cardRect.left + Scale(34), item.cardRect.top + Scale(4),
                              item.cardRect.right - Scale(8), item.cardRect.top + Scale(21));
        DrawTextValue(dc, item.title, title, g.titleFont, P().mainText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT process = MakeRect(title.left, item.cardRect.top + Scale(20), title.right,
                                item.cardRect.top + Scale(37));
        DrawTextValue(dc, FileNameFromPath(item.processPath), process, g.smallFont,
                      P().processText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (item.isWindowCue && displayIcon) {
            int selfIconSize = Scale(58);
            int selfIconLeft = item.previewRect.left +
                (RectWidth(item.previewRect) - selfIconSize) / 2;
            int selfIconTop = item.previewRect.top +
                (RectHeight(item.previewRect) - selfIconSize) / 2;
            dc.AppIcon(g.appLargeIcon ? g.appLargeIcon : displayIcon,
                MakeRect(selfIconLeft,selfIconTop,selfIconLeft+selfIconSize,selfIconTop+selfIconSize));
        }
    }
    dc.PopClip();

    if (g.maxScroll > 0) {
        FillRounded(dc, g.scrollbarRect, Scale(5), P().scrollTrack);
        FillRounded(dc, g.scrollbarThumb, Scale(5), P().scrollThumb);
    }

    FillRounded(dc, g.bindingRect, Scale(12), P().panelBackground);
    StrokeRounded(dc, g.bindingRect, Scale(12), Scale(1), P().panelBorder);
    bool hasSelection = g.selected >= 0 && g.selected < static_cast<int>(g.windows.size());
    int bindingIndex = hasSelection ? BindingIndexForWindow(g.windows[g.selected].handle) : -1;
    if (hasSelection) {
        const WindowInfo& item = g.windows[g.selected];
        int selectedIcon = Scale(26);
        HICON selectedWindowIcon = item.isWindowCue ? g.appIcon : item.icon;
        if (selectedWindowIcon) {
            int x=g.bindingRect.left+Scale(16), y=g.bindingRect.top+(RectHeight(g.bindingRect)-selectedIcon)/2;
            dc.AppIcon(selectedWindowIcon,MakeRect(x,y,x+selectedIcon,y+selectedIcon));
        }
        RECT selectedTitle = MakeRect(g.bindingRect.left + Scale(56), g.bindingRect.top + Scale(12),
                                      g.hotkeyRect.left - Scale(15), g.bindingRect.top + Scale(32));
        DrawTextValue(dc, item.title, selectedTitle, g.titleFont, P().mainText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT selectedPath = MakeRect(selectedTitle.left, g.bindingRect.top + Scale(32),
                                     selectedTitle.right, g.bindingRect.bottom - Scale(10));
        DrawTextValue(dc, item.processPath, selectedPath, g.smallFont, P().mutedText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        RECT placeholder = MakeRect(g.bindingRect.left + Scale(55), g.bindingRect.top,
                                    g.hotkeyRect.left - Scale(15), g.bindingRect.bottom);
        DrawTextValue(dc, T().selectWindow, placeholder, g.bodyFont, P().mutedText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    FillRounded(dc, g.hotkeyRect, Scale(8), P().inputBackground);
    COLORREF inputBorder = g.inputError ? P().errorBorder :
        (g.capturing ? P().selectedBorder : P().inputBorder);
    StrokeRounded(dc, g.hotkeyRect, Scale(8), Scale(1), inputBorder);
    RECT inputText = g.hotkeyRect;
    InflateRect(&inputText, -Scale(8), 0);
    DrawTextValue(dc, g.inputText, inputText, g.bodyFont,
                  hasSelection ? P().mainText : P().placeholderText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    COLORREF buttonColor = !hasSelection ? P().disabledButton :
        (bindingIndex >= 0 ? P().unbindButton : P().bindButton);
    COLORREF buttonBorder = !hasSelection ? P().disabledButtonBorder :
        (bindingIndex >= 0 ? P().unbindButtonBorder : P().bindButtonBorder);
    FillRounded(dc, g.bindButtonRect, Scale(8), buttonColor);
    StrokeRounded(dc, g.bindButtonRect, Scale(8), Scale(1), buttonBorder);
    DrawTextValue(dc, bindingIndex >= 0 ? T().unbind : T().bind, g.bindButtonRect,
                  g.bodyFont, hasSelection ? (bindingIndex>=0 ? P().unbindText : P().buttonText) : P().disabledText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if(!IsZoomed(g.hwnd)) dc.Stroke(client,0,1,g.windowActive ? P().activeWindowBorder : P().inactiveWindowBorder);
    if(clipped) dc.PopRegion();
    HRESULT result=dc.End();
    g.renderingFailed=FAILED(result);
    if(SUCCEEDED(result)) {g.renderRetries=0;KillTimer(g.hwnd,kRenderRetryTimer);}
    if(result==D2DERR_RECREATE_TARGET) InvalidateRect(g.hwnd,nullptr,FALSE);
}

void AddTrayIcon() {
    ZeroMemory(&g.tray, sizeof(g.tray));
    g.tray.cbSize = sizeof(g.tray);
    g.tray.hWnd = g.hwnd;
    g.tray.uID = 1;
    g.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g.tray.uCallbackMessage = kTrayMessage;
    g.tray.hIcon = g.appIcon;
    wcscpy_s(g.tray.szTip, L"WindowCue");
    if (Shell_NotifyIconW(NIM_ADD, &g.tray)) {
        KillTimer(g.hwnd, kTrayRetryTimer);
        g.trayRetries = 0;
    } else if (g.trayRetries < 12) {
        SetTimer(g.hwnd, kTrayRetryTimer, 5000, nullptr);
    }
}

void ShowMainWindow() {
    ShowWindow(g.hwnd, SW_SHOW);
    if (IsIconic(g.hwnd)) ShowWindow(g.hwnd, SW_RESTORE);
    SetForegroundWindow(g.hwnd);
    RefreshWindows(true);
}

void HideToTray() {
    EndCapture(true);
    HideSettingsWindow();
    ResetCaptionInteraction();
    ShowWindow(g.hwnd, SW_HIDE);
    UpdateThumbnails();
}

void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, T().openSettings);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, T().exit);
    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(g.hwnd);
    UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                  point.x, point.y, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    if (command == 1) ShowMainWindow();
    else if (command == 2) {
        g.allowExit = true;
        DestroyWindow(g.hwnd);
    }
}

void ExitApplication() {
    g.allowExit = true;
    DestroyWindow(g.hwnd);
}

void HandleKey(UINT virtualKey) {
    if (!g.capturing) return;
    if (virtualKey == VK_ESCAPE) {
        g.captured = false;
        g.inputText = T().clickToRecord;
        g.inputError = false;
        EndCapture(true);
        InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
        return;
    }
    if (virtualKey == VK_CONTROL || virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL ||
        virtualKey == VK_MENU || virtualKey == VK_LMENU || virtualKey == VK_RMENU ||
        virtualKey == VK_SHIFT || virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT ||
        virtualKey == VK_LWIN || virtualKey == VK_RWIN) {
        return;
    }
    UINT modifiers = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) modifiers |= MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) modifiers |= MOD_ALT;
    if (GetKeyState(VK_SHIFT) & 0x8000) modifiers |= MOD_SHIFT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) modifiers |= MOD_WIN;
    if (!modifiers) {
        g.captured = false;
        g.inputText = T().useModifier;
        g.inputError = true;
    } else {
        g.captured = true;
        g.capturedModifiers = modifiers;
        g.capturedKey = virtualKey;
        g.inputText = BuildHotkeyText(modifiers, virtualKey);
        g.inputError = false;
    }
    InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
}

int HitCard(POINT point) {
    if (!PointInside(g.previewInner, point)) return -1;
    for (size_t i = 0; i < g.windows.size(); ++i) {
        if (PointInside(g.windows[i].cardRect, point)) return static_cast<int>(i);
    }
    return -1;
}

void HandleClick(POINT point) {
    int card = HitCard(point);
    if (card >= 0) {
        SelectWindowCard(card);
        return;
    }
    bool hasSelection = g.selected >= 0 && g.selected < static_cast<int>(g.windows.size());
    if (hasSelection && PointInside(g.hotkeyRect, point) &&
        BindingIndexForWindow(g.windows[g.selected].handle) < 0) {
        BeginCapture();
        return;
    }
    if (hasSelection && PointInside(g.bindButtonRect, point)) {
        BindOrUnbindSelected();
    }
}

void ApplyDwmFrame(HWND hwnd) {
    auto result=ConfigureOpaqueChrome(hwnd);
    if(hwnd==g.hwnd) g.chromeStatus=result;
    else g.settingsChromeStatus=result;
}
void ApplyDwmBorder(HWND) {}

int SettingsScale(int value) {
    return MulDiv(value, static_cast<int>(g.settingsDpi), 96);
}

const wchar_t* StartupStateText(StartupState state) {
    switch (state) {
    case StartupState::Enabled:
    case StartupState::Disabled: return L"";
    case StartupState::DisabledByUser: return T().startupWindowsDisabled;
    case StartupState::DisabledByPolicy: return T().startupPolicyDisabled;
    default: return T().startupUnavailable;
    }
}

void UpdateStartupControls() {
    if (!g.settingsWindow) return;
    bool enabled = g.startupStatus.state == StartupState::Enabled;
    bool windowsDisabled = g.startupStatus.state == StartupState::DisabledByUser;
    bool canChange = enabled || g.startupStatus.state == StartupState::Disabled || windowsDisabled;
    // Native button text supplies an accessible action, even though its visible
    // surface is a compact owner-drawn switch.
    SetWindowTextW(g.startupButton, windowsDisabled ? T().startupWindowsSettings :
                   (enabled ? T().startupDisable : T().startupEnable));
    EnableWindow(g.startupButton, canChange ? TRUE : FALSE);
    NotifyToggle(g.startupButton,enabled);
    g.settingsMessage = StartupStateText(g.startupStatus.state);
    if (!g.settingsMessage.empty() && !g.startupStatus.detail.empty())
        g.settingsMessage += L"\r\n" + g.startupStatus.detail;
    SetWindowTextW(g.startupStatusLabel, g.settingsMessage.c_str());
    ShowWindow(g.startupStatusLabel, g.settingsMessage.empty() ? SW_HIDE : SW_SHOWNA);
    LayoutSettingsControls();
    InvalidateRect(g.startupButton, nullptr, FALSE);
}

void RefreshStartupControls() {
    g.startupStatus = QueryStartupStatus();
    UpdateStartupControls();
}

void RefreshSettingsResources() {
    if (g.settingsFont) DeleteObject(g.settingsFont);
    g.settingsFont = CreateFontW(-MulDiv(9, static_cast<int>(g.settingsDpi), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY,
        VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    for (HWND control : {g.startupButton, g.githubButton, g.startupLabel,
                          g.startupStatusLabel}) {
        if (control) SendMessageW(control, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g.settingsFont), TRUE);
    }
}

void ApplySettingsTheme() {
    if (!g.settingsWindow) return;
    ApplyDwmFrame(g.settingsWindow);
    RedrawWindow(g.settingsWindow, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void LayoutSettingsControls() {
    if (!g.settingsWindow) return;
    RECT client{};
    GetClientRect(g.settingsWindow, &client);
    int margin = SettingsScale(18);
    int header = SettingsScale(34);
    int width = RectWidth(client) - margin * 2;
    MoveWindow(g.startupLabel, margin, header + SettingsScale(18), width - SettingsScale(52),
               SettingsScale(24), TRUE);
    MoveWindow(g.startupButton, client.right - margin - SettingsScale(36), header + SettingsScale(20),
               SettingsScale(36), SettingsScale(20), TRUE);
    int footerTop = header + SettingsScale(60);
    if (!g.settingsMessage.empty()) {
        // Only Windows restrictions and action failures need an explanation.
        // Measure it at the current DPI so it never consumes space when empty.
        int height = std::max(SettingsScale(20),g.settingsRenderer.TextHeight(
            g.settingsMessage,g.settingsFont,width,g.settingsDpi));
        MoveWindow(g.startupStatusLabel, margin, header + SettingsScale(52), width, height, TRUE);
        footerTop = header + SettingsScale(52) + height + SettingsScale(12);
    }
    // Align the icon and switch optically; the GitHub hit target stays generous
    // while its visible surface has no text, tile, or border.
    MoveWindow(g.githubButton, client.right - margin - SettingsScale(34), footerTop,
               SettingsScale(32), SettingsScale(32), TRUE);
    int height = footerTop + SettingsScale(32) + SettingsScale(14);
    if (RectHeight(client) != height) {
        RECT bounds{};
        GetWindowRect(g.settingsWindow, &bounds);
        SetWindowPos(g.settingsWindow, nullptr, 0, 0, RectWidth(bounds),
            RectHeight(bounds) + height - RectHeight(client),
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void HideSettingsWindow() {
    if (!g.settingsWindow) return;
    ShowWindow(g.settingsWindow, SW_HIDE);
    PopCaptionTooltip();
    InvalidateRect(g.hwnd, &g.settingsButtonRect, FALSE);
}

void OpenSettingsLink(const wchar_t* link) {
    HINSTANCE result = ShellExecuteW(g.settingsWindow, L"open", link,
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        g.settingsMessage = T().linkError;
        SetWindowTextW(g.startupStatusLabel, g.settingsMessage.c_str());
        ShowWindow(g.startupStatusLabel, SW_SHOWNA);
        LayoutSettingsControls();
    }
}

void ChangeStartupFromSettings() {
    if (g.startupStatus.state == StartupState::DisabledByUser) {
        OpenSettingsLink(L"ms-settings:startupapps");
        return;
    }
    if (g.startupStatus.state != StartupState::Enabled &&
        g.startupStatus.state != StartupState::Disabled) return;
    g.startupStatus = SetStartupEnabled(g.startupStatus.state != StartupState::Enabled);
    UpdateStartupControls();
    if (g.startupStatus.state == StartupState::DisabledByUser) {
        OpenSettingsLink(L"ms-settings:startupapps");
    }
}

void DrawSettingsButton(const DRAWITEMSTRUCT& item) {
    bool enabled = (item.itemState & ODS_DISABLED) == 0;
    bool pressed = (item.itemState & ODS_SELECTED) != 0;
    RECT rect = item.rcItem;
    auto& dc = item.CtlID == kStartupButtonId ? g.startupRenderer : g.githubRenderer;
    if(!dc.Begin(item.hwndItem,g.settingsDpi,rect,UseSoftwareRenderer())) return;
    dc.Fill(rect,P().windowBackground);
    if (item.CtlID == kStartupButtonId) {
        bool on = g.startupStatus.state == StartupState::Enabled;
        COLORREF fill = on ? P().selectedBorder : P().disabledButton;
        if (pressed && enabled) fill = BlendCaptionColor(fill, P().mainText, 0.12);
        FillRounded(dc, rect, SettingsScale(20), fill);
        StrokeRounded(dc, rect, SettingsScale(20), SettingsScale(1),
                       enabled ? P().hoverBorder : P().disabledButtonBorder);
        int knob = SettingsScale(14);
        int left = on ? rect.right - SettingsScale(3) - knob : rect.left + SettingsScale(3);
        int top = rect.top + (RectHeight(rect) - knob) / 2;
        RECT knobRect = MakeRect(left, top, left + knob, top + knob);
        FillRounded(dc, knobRect, knob, on ? (HighContrastEnabled() ? P().buttonText : RGB(245,248,252)) : P().mutedText);
    } else {
        dc.Icon(VectorIcon::GitHub,rect,P().mainText,20,pressed ? 1.f : 0.f);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, -SettingsScale(2), -SettingsScale(2));
        bool switchOn = item.CtlID == kStartupButtonId &&
                        g.startupStatus.state == StartupState::Enabled;
        StrokeRounded(dc, focus,
            item.CtlID == kStartupButtonId ? SettingsScale(16) : SettingsScale(6),
            SettingsScale(1), switchOn ? RGB(245, 248, 252) : P().selectedBorder);
    }
    if(dc.End()==D2DERR_RECREATE_TARGET) InvalidateRect(item.hwndItem,nullptr,FALSE);
}

LRESULT CALLBACK SettingsLabelProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                        UINT_PTR, DWORD_PTR) {
    auto& dc=hwnd==g.startupLabel ? g.startupLabelRenderer : g.statusRenderer;
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_PAINT) {
        PAINTSTRUCT paint{};BeginPaint(hwnd,&paint);RECT rect{};GetClientRect(hwnd,&rect);
        wchar_t text[2048]{};GetWindowTextW(hwnd,text,static_cast<int>(std::size(text)));
        if(dc.Begin(hwnd,g.settingsDpi,paint.rcPaint,UseSoftwareRenderer())) {
            dc.Fill(rect,P().windowBackground);
            dc.Text(text,rect,g.settingsFont,hwnd==g.startupLabel ? P().mainText : P().mutedText,
                hwnd==g.startupLabel ? DT_LEFT|DT_SINGLELINE|DT_VCENTER : DT_LEFT|DT_WORDBREAK);
            if(dc.End()==D2DERR_RECREATE_TARGET) InvalidateRect(hwnd,nullptr,FALSE);
        }
        EndPaint(hwnd,&paint);return 0;
    }
    if(message==WM_NCDESTROY) dc.Discard();
    return DefSubclassProc(hwnd,message,wp,lp);
}

LRESULT CALLBACK SettingsButtonProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
                                         UINT_PTR, DWORD_PTR) {
    if(message==WM_GETOBJECT && static_cast<LONG>(lp)==UiaRootObjectId)
        return AutomationObject(hwnd,wp,lp,hwnd==g.startupButton);
    if(message==WM_ERASEBKGND) return 1;
    if(message==WM_NCDESTROY) {
        (hwnd==g.startupButton ? g.startupRenderer : g.githubRenderer).Discard();
        RemovePropW(hwnd,L"WindowCue.ToggleState");
    }
    return DefSubclassProc(hwnd,message,wp,lp);
}

LRESULT CALLBACK SettingsProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        if(wParam) return 0;
        break;
    case WM_NCPAINT: return 0;
    case WM_NCHITTEST: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&p);
        RECT r{};GetClientRect(hwnd,&r);
        if(p.y<SettingsScale(34) && p.x<r.right-SettingsScale(40)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONUP: {
        RECT r{};GetClientRect(hwnd,&r);
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        bool close=g.settingsClosePressed && p.y>=0 && p.y<SettingsScale(34) &&
                   p.x>=r.right-SettingsScale(40) && p.x<r.right;
        g.settingsClosePressed=false;ReleaseCapture();
        InvalidateRect(hwnd,nullptr,FALSE);
        if(close) SendMessageW(hwnd,WM_CLOSE,0,0);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT r{};GetClientRect(hwnd,&r);POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        if(p.y<SettingsScale(34) && p.x>=r.right-SettingsScale(40)) {
            g.settingsClosePressed=true;SetCapture(hwnd);InvalidateRect(hwnd,nullptr,FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        RECT r{};GetClientRect(hwnd,&r);POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        bool hot=p.y>=0 && p.y<SettingsScale(34) && p.x>=r.right-SettingsScale(40) && p.x<r.right;
        if(hot!=g.settingsCloseHot) {g.settingsCloseHot=hot;InvalidateRect(hwnd,nullptr,FALSE);}
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0};TrackMouseEvent(&track);return 0;
    }
    case WM_MOUSELEAVE:
        g.settingsCloseHot=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_CAPTURECHANGED:
        g.settingsClosePressed=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;
    case WM_CREATE:
        g.settingsWindow = hwnd;
        g.settingsDpi = QueryWindowDpi(hwnd);
        g.startupLabel = CreateWindowW(L"STATIC", T().startup, WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            0, 0, 0, 0, hwnd, nullptr, g.instance, nullptr);
        g.startupStatusLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
            0, 0, 0, 0, hwnd, nullptr, g.instance, nullptr);
        SetWindowSubclass(g.startupLabel,SettingsLabelProcedure,1,0);
        SetWindowSubclass(g.startupStatusLabel,SettingsLabelProcedure,1,0);
        g.startupButton = CreateWindowW(L"BUTTON", T().startup,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartupButtonId)),
            g.instance, nullptr);
        g.githubButton = CreateWindowW(L"BUTTON", T().github,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGitHubButtonId)),
            g.instance, nullptr);
        SetWindowSubclass(g.startupButton,SettingsButtonProcedure,2,0);
        SetWindowSubclass(g.githubButton,SettingsButtonProcedure,2,0);
        RefreshSettingsResources();
        ApplySettingsTheme();
        RefreshStartupControls();
        LayoutSettingsControls();
        return 0;
    case WM_SIZE:
        LayoutSettingsControls();
        return 0;
    case WM_DPICHANGED: {
        g.settingsDpi = HIWORD(wParam);
        RefreshSettingsResources();
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     RectWidth(*suggested), RectHeight(*suggested), SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutSettingsControls();
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) RefreshStartupControls();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            if (LOWORD(wParam) == kStartupButtonId) ChangeStartupFromSettings();
            else if (LOWORD(wParam) == kGitHubButtonId) OpenSettingsLink(kGitHubUrl);
        }
        return 0;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wParam) == g.githubButton && LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        DrawSettingsButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        RECT r{};GetClientRect(hwnd,&r);
        auto& dc=g.settingsRenderer;
        if(dc.Begin(hwnd,g.settingsDpi,paint.rcPaint,UseSoftwareRenderer())) {
            dc.Fill(r,P().windowBackground);
            dc.Text(T().settings,MakeRect(SettingsScale(12),0,r.right-SettingsScale(44),SettingsScale(34)),
                g.settingsFont,P().titleText,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            RECT close=MakeRect(r.right-SettingsScale(40),0,r.right,SettingsScale(34));
            if(g.settingsCloseHot) dc.Fill(close,RGB(196,43,44));
            dc.Icon(VectorIcon::Close,close,g.settingsCloseHot ? RGB(255,255,255) : P().mainText,14);
            dc.Stroke(r,0,1,P().inactiveWindowBorder);
            if(dc.End()==D2DERR_RECREATE_TARGET) InvalidateRect(hwnd,nullptr,FALSE);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        HideSettingsWindow();
        SetFocus(g.hwnd);
        return 0;
    case WM_DESTROY:
        if (g.settingsFont) DeleteObject(g.settingsFont);
        g.settingsRenderer.Shutdown();g.startupRenderer.Shutdown();g.githubRenderer.Shutdown();
        g.startupLabelRenderer.Shutdown();g.statusRenderer.Shutdown();
        g.settingsFont = nullptr;
        g.settingsWindow = g.startupButton = g.githubButton = nullptr;
        g.startupLabel = g.startupStatusLabel = nullptr;
        g.settingsMessage.clear();
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowSettingsWindow() {
    PopCaptionTooltip();
    EndCapture(true);
    if (!g.settingsWindow) {
        WNDCLASSW wc{};
        wc.hInstance = g.instance;
        wc.lpfnWndProc = SettingsProcedure;
        wc.lpszClassName = kSettingsClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        RECT size = MakeRect(0, 0, Scale(kSettingsWidth), Scale(kSettingsHeight+34));
        constexpr DWORD style = WS_POPUP | WS_SYSMENU;
        using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        auto adjust = reinterpret_cast<AdjustForDpi>(GetProcAddress(
            GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
        if (adjust) adjust(&size, style, FALSE, WS_EX_TOOLWINDOW, g.dpi);
        else AdjustWindowRectEx(&size, style, FALSE, WS_EX_TOOLWINDOW);
        POINT initialAnchor{g.settingsButtonRect.right, g.settingsButtonRect.bottom};
        ClientToScreen(g.hwnd, &initialAnchor);
        // Create on the owner's monitor so the first layout uses its DPI;
        // creating at (0, 0) can otherwise scale the window twice on first move.
        CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass, T().settings, style,
            initialAnchor.x - RectWidth(size), initialAnchor.y,
            RectWidth(size), RectHeight(size), g.hwnd, nullptr, g.instance, nullptr);
    }
    if (!g.settingsWindow) return;
    RefreshStartupControls();
    RECT bounds{};
    GetWindowRect(g.settingsWindow, &bounds);
    POINT anchor{g.settingsButtonRect.right, g.settingsButtonRect.bottom};
    ClientToScreen(g.hwnd, &anchor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    int left = std::clamp(anchor.x - RectWidth(bounds), monitor.rcWork.left,
                          std::max(monitor.rcWork.left, monitor.rcWork.right - RectWidth(bounds)));
    int top = std::clamp(anchor.y, monitor.rcWork.top,
                         std::max(monitor.rcWork.top, monitor.rcWork.bottom - RectHeight(bounds)));
    SetWindowPos(g.settingsWindow, HWND_TOP, left, top, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(g.settingsWindow);
    SetFocus(IsWindowEnabled(g.startupButton) ? g.startupButton : g.githubButton);
    InvalidateRect(g.hwnd, &g.settingsButtonRect, FALSE);
}

bool HandleSettingsMessage(MSG& message) {
    if (!g.settingsWindow || !IsWindowVisible(g.settingsWindow) ||
        (message.hwnd != g.settingsWindow && !IsChild(g.settingsWindow, message.hwnd))) return false;
    if (message.message == WM_KEYDOWN) {
        if (message.wParam == VK_ESCAPE) {
            HideSettingsWindow();
            SetFocus(g.hwnd);
            return true;
        }
        if (message.wParam == VK_RETURN) {
            HWND focused = GetFocus();
            if (focused == g.startupButton || focused == g.githubButton) {
                SendMessageW(focused, BM_CLICK, 0, 0);
                return true;
            }
        }
    }
    return IsDialogMessageW(g.settingsWindow, &message) != FALSE;
}

void ToggleTheme() {
    PopCaptionTooltip();
    g.darkMode = !g.darkMode;
    UpdateCaptionTooltipText();
    SaveDarkModePreference(g.darkMode);
    ApplySettingsTheme();
    RedrawWindow(g.hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

int CaptionButtonAt(POINT client) {
    return PointInside(g.minButtonRect,client) ? 1 : PointInside(g.maxButtonRect,client) ? 2 :
           PointInside(g.closeButtonRect,client) ? 3 : 0;
}
void InvokeCaptionButton(int id) {
    if(id==1) SendMessageW(g.hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);
    if(id==2) SendMessageW(g.hwnd,WM_SYSCOMMAND,IsZoomed(g.hwnd)?SC_RESTORE:SC_MAXIMIZE,0);
    if(id==3) SendMessageW(g.hwnd,WM_CLOSE,0,0);
}

LRESULT HitTest(POINT screenPoint) {
    POINT client = screenPoint;
    ScreenToClient(g.hwnd, &client);
    LRESULT border=ResizeBorderHit(g.hwnd,g.dpi,client);
    if(border!=HTNOWHERE) return border;
    int button=CaptionButtonAt(client);
    if(button) return button==1 ? HTMINBUTTON : button==2 ? HTMAXBUTTON : HTCLOSE;
    if (PointInside(g.themeButtonRect, client) ||
        PointInside(g.settingsButtonRect, client)) return HTCLIENT;
    if (client.y < Scale(34)) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g.taskbarCreatedMessage && message == g.taskbarCreatedMessage) {
        g.trayRetries = 0;
        AddTrayIcon();
        return 0;
    }
    switch (message) {
    case WM_NCPAINT: return 0;
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd,message,wParam,-1);
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return HitTest(point);
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = Scale(760);
        info->ptMinTrackSize.y = Scale(310);
        SetMaximizedWorkArea(hwnd,*info);
        return 0;
    }
    case WM_CREATE:
        g.hwnd = hwnd;
        g.dpi = QueryWindowDpi(hwnd);
        RefreshSystemTheme();
        CreateFonts();
        RefreshThemeIcons();
        CreateCaptionTooltips();
        RefreshCaptionAnimationPolicy();
        ApplyDwmFrame(hwnd);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
        RefreshSystemTheme();
        ApplyDwmFrame(hwnd);
        ApplySettingsTheme();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) ResetCaptionInteraction();
        if (wParam != SIZE_MINIMIZED) ResizeForContent();
        Layout();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateThumbnails();
        return 0;
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wParam);
        CreateFonts();
        RefreshThemeIcons();
        ApplyDwmFrame(hwnd);
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     RectWidth(*suggested), RectHeight(*suggested),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ResizeForContent();
        return 0;
    }
    case WM_EXITSIZEMOVE:
    case WM_DISPLAYCHANGE:
        ResizeForContent();
        Layout();
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateThumbnails();
        return 0;
    case WM_SETTINGCHANGE:
        RefreshSystemTheme();
        RefreshCaptionAnimationPolicy();
        ApplySettingsTheme();
        InvalidateRect(hwnd,nullptr,FALSE);
        if (wParam == SPI_SETWORKAREA) {
            ResizeForContent();
            Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateThumbnails();
        }
        return 0;
    case WM_SHOWWINDOW:
        if (!wParam) ResetCaptionInteraction();
        if (g.captionTooltips) SendMessageW(g.captionTooltips, TTM_ACTIVATE, wParam != 0, 0);
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto region=WindowPaintRegion(hwnd);
        HDC dc = BeginPaint(hwnd, &paint);
        Layout();
        DrawInterface(dc, paint.rcPaint,region);
        EndPaint(hwnd, &paint);
        UpdateThumbnails();
        return 0;
    }
    case WM_GETTITLEBARINFOEX: {
        auto* info=reinterpret_cast<TITLEBARINFOEX*>(lParam);
        if(!info || info->cbSize!=sizeof(*info)) return 0;
        info->rcTitleBar=g.titleRect;
        MapWindowPoints(hwnd,nullptr,reinterpret_cast<POINT*>(&info->rcTitleBar),2);
        for(int i=0;i<6;++i) { info->rgstate[i]=STATE_SYSTEM_INVISIBLE;info->rgrect[i]={}; }
        info->rgstate[0]=0;
        int indexes[]={2,3,5};RECT rects[]={g.minButtonRect,g.maxButtonRect,g.closeButtonRect};
        for(int i=0;i<3;++i) { info->rgstate[indexes[i]]=STATE_SYSTEM_FOCUSABLE;
            info->rgrect[indexes[i]]=rects[i];
            MapWindowPoints(hwnd,nullptr,reinterpret_cast<POINT*>(&info->rgrect[indexes[i]]),2); }
        return 0;
    }
    case WM_NCMOUSEMOVE: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&p);
        int hot=CaptionButtonAt(p);
        if(hot!=g.captionHot) { g.captionHot=hot;InvalidateRect(hwnd,&g.titleRect,FALSE); }
        TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE|TME_NONCLIENT,hwnd,0};TrackMouseEvent(&tracking);
        // DefWindowProc receives HTMAXBUTTON to preserve the OS snap-menu path.
        return DefWindowProcW(hwnd,message,wParam,lParam);
    }
    case WM_NCMOUSELEAVE:
        g.captionHot=0;InvalidateRect(hwnd,&g.titleRect,FALSE);return 0;
    case WM_NCLBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&p);
        int id=CaptionButtonAt(p);
        if(id) {g.captionPressed=id;SetCapture(hwnd);InvalidateRect(hwnd,&g.titleRect,FALSE);return 0;}
        break;
    }
    case WM_NCLBUTTONUP: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};ScreenToClient(hwnd,&p);
        int id=CaptionButtonAt(p),pressed=g.captionPressed;g.captionPressed=0;ReleaseCapture();
        if(id && id==pressed) InvokeCaptionButton(id);
        InvalidateRect(hwnd,&g.titleRect,FALSE);return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if(g.captionPressed) {g.captionHot=CaptionButtonAt(point);InvalidateRect(hwnd,&g.titleRect,FALSE);return 0;}
        if(g.captionHot) {g.captionHot=0;InvalidateRect(hwnd,&g.titleRect,FALSE);}
        if (g.draggingScrollbar) {
            int trackTravel = RectHeight(g.scrollbarRect) - RectHeight(g.scrollbarThumb);
            if (trackTravel > 0 && g.maxScroll > 0) {
                int delta = point.y - g.scrollbarDragY;
                g.scrollOffset = std::clamp(
                    g.scrollbarStartOffset + delta * g.maxScroll / trackTravel,
                    0, g.maxScroll);
                Layout();
                InvalidateRect(hwnd, &g.previewOuter, FALSE);
                UpdateThumbnails();
            }
            return 0;
        }
        int hovered = HitCard(point);
        if (hovered != g.hovered) {
            int previous = g.hovered;
            g.hovered = hovered;
            InvalidateCardDecoration(previous);
            InvalidateCardDecoration(hovered);
        }
        SetCaptionHovered(false, PointInside(g.themeButtonRect, point));
        SetCaptionHovered(true, PointInside(g.settingsButtonRect, point));
        if (!g.trackingMouse) {
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tracking);
            g.trackingMouse = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.trackingMouse = false;
        InvalidateCardDecoration(g.hovered);
        g.hovered = -1;
        SetCaptionHovered(false, false);
        SetCaptionHovered(true, false);
        PopCaptionTooltip();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointInside(g.settingsButtonRect, point)) {
            g.settingsButtonPressed = true;
            SetCaptionHovered(true, true);
            PopCaptionTooltip();
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
            return 0;
        }
        if (PointInside(g.themeButtonRect, point)) {
            g.themeButtonPressed = true;
            SetCaptionHovered(false, true);
            PopCaptionTooltip();
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
            return 0;
        }
        if (g.maxScroll > 0 && PointInside(g.scrollbarThumb, point)) {
            g.draggingScrollbar = true;
            g.scrollbarDragY = point.y;
            g.scrollbarStartOffset = g.scrollOffset;
            SetCapture(hwnd);
            return 0;
        }
        if (g.maxScroll > 0 && PointInside(g.scrollbarRect, point)) {
            int page = std::max(Scale(54), RectHeight(g.previewInner) - Scale(36));
            g.scrollOffset = std::clamp(
                g.scrollOffset + (point.y < g.scrollbarThumb.top ? -page : page),
                0, g.maxScroll);
            Layout();
            InvalidateRect(hwnd, &g.previewOuter, FALSE);
            UpdateThumbnails();
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if(g.captionPressed) {
            int id=g.captionPressed;g.captionPressed=0;ReleaseCapture();
            if(CaptionButtonAt(point)==id) InvokeCaptionButton(id);
            InvalidateRect(hwnd,&g.titleRect,FALSE);return 0;
        }
        if (g.settingsButtonPressed) {
            bool open = PointInside(g.settingsButtonRect, point);
            g.settingsButtonPressed = false;
            SetCaptionHovered(true, open);
            ReleaseCapture();
            if (open) {
                if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) HideSettingsWindow();
                else ShowSettingsWindow();
            }
            InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
            return 0;
        }
        if (g.themeButtonPressed) {
            bool toggle = PointInside(g.themeButtonRect, point);
            g.themeButtonPressed = false;
            SetCaptionHovered(false, toggle);
            ReleaseCapture();
            if (toggle) ToggleTheme();
            else InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
            return 0;
        }
        if (g.draggingScrollbar) {
            g.draggingScrollbar = false;
            ReleaseCapture();
            return 0;
        }
        HandleClick(point);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if(g.captionPressed) InvalidateRect(hwnd,&g.titleRect,FALSE);
        g.captionPressed=0;
        g.draggingScrollbar = false;
        if (g.settingsButtonPressed) InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
        if (g.themeButtonPressed) InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
        g.settingsButtonPressed = false;
        g.themeButtonPressed = false;
        return 0;
    case WM_MOUSEWHEEL: {
        if (!g.maxScroll) return 0;
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g.scrollOffset = std::clamp(g.scrollOffset - delta * Scale(54) / WHEEL_DELTA,
                                    0, g.maxScroll);
        Layout();
        InvalidateRect(hwnd, &g.previewOuter, FALSE);
        UpdateThumbnails();
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (!g.capturing && wParam == VK_OEM_COMMA && (GetKeyState(VK_CONTROL) & 0x8000)) {
            ShowSettingsWindow();
            return 0;
        }
        HandleKey(static_cast<UINT>(wParam));
        return g.capturing ? 0 : DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_ACTIVATE: {
        g.windowActive = LOWORD(wParam) != WA_INACTIVE;
        bool wasCapturing = g.capturing;
        if (!g.windowActive) {
            EndCapture(true);
            PopCaptionTooltip();
        }
        // Focus changes affect only chrome. Re-extending the glass frame and
        // repainting preview canvases here races the unchanged DWM thumbnails.
        ApplyDwmBorder(hwnd);
        InvalidateWindowChrome();
        if (wasCapturing) InvalidateRect(hwnd, &g.bindingRect, FALSE);
        // Re-submit the bounded transform when DWM changes activation surfaces;
        // decoration paints still exclude previews and thumbnail handles survive.
        UpdateThumbnails(true);
        return 0;
    }
    case WM_DWMCOMPOSITIONCHANGED:
        ApplyDwmFrame(hwnd);
        return 0;
    case WM_HOTKEY: {
        int id = static_cast<int>(wParam);
        for (size_t i = 0; i < g.bindings.size(); ++i) {
            if (g.bindings[i].hotkeyId != id) continue;
            if (!IsWindow(g.bindings[i].target)) {
                RemoveBinding(i);
            } else if (g.bindings[i].settings.isWindowCue) {
                HWND foreground = GetForegroundWindow();
                if (IsWindowVisible(hwnd) && !IsIconic(hwnd) &&
                    (foreground == hwnd || foreground == g.settingsWindow)) {
                    HideToTray();
                } else {
                    ShowMainWindow();
                }
            } else {
                ToggleWindow(g.bindings[i].target);
            }
            break;
        }
        return 0;
    }
    case WM_TIMER:
        if(wParam==kRenderRetryTimer) {
            KillTimer(hwnd,kRenderRetryTimer);++g.renderRetries;
            if(IsWindowVisible(hwnd) && !IsIconic(hwnd)) InvalidateRect(hwnd,nullptr,FALSE);
        } else if (wParam == kCaptionAnimationTimer) {
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
                ResetCaptionInteraction();
                return 0;
            }
            ULONGLONG now = GetTickCount64();
            if (AdvanceCaptionHover(g.settingsHoverAnimation, now)) {
                InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
            }
            if (AdvanceCaptionHover(g.themeHoverAnimation, now)) {
                InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
            }
            SyncCaptionAnimationTimer();
        } else if (wParam == kTrayRetryTimer) {
            if (++g.trayRetries >= 12) KillTimer(hwnd, kTrayRetryTimer);
            AddTrayIcon();
        } else if (wParam == kThumbnailTimer) UpdateThumbnails();
        else if (wParam == kRefreshTimer) {
            ValidateBindings();
            if (!g.capturing) RestoreBindings();
            if (IsWindowVisible(hwnd) && GetForegroundWindow() == hwnd) RefreshWindows(false);
        }
        return 0;
    case kTrayMessage:
        if (lParam == WM_LBUTTONDBLCLK) ShowMainWindow();
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE || (wParam & 0xFFF0) == SC_MINIMIZE) {
            HideToTray();
            return 0;
        }
        break;
    case WM_CLOSE:
        if (!g.allowExit) {
            HideToTray();
            return 0;
        }
        break;
    case WM_DESTROY:
        ResetCaptionInteraction();
        if (g.captionTooltips) DestroyWindow(g.captionTooltips);
        g.captionTooltips = nullptr;
        KillTimer(hwnd, kThumbnailTimer);
        KillTimer(hwnd, kRefreshTimer);
        KillTimer(hwnd, kTrayRetryTimer);
        KillTimer(hwnd, kRenderRetryTimer);
        if (g.settingsWindow) DestroyWindow(g.settingsWindow);
        UnregisterAllBindings();
        Shell_NotifyIconW(NIM_DELETE, &g.tray);
        ReleaseWindows(g.windows);
        DeleteFonts();
        DeleteThemeIcons();
        g.renderer.Shutdown();
        if (g.appIcon) DestroyIcon(g.appIcon);
        if (g.appLargeIcon) DestroyIcon(g.appLargeIcon);
        g.appLargeIcon = g.appIcon = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    using SetDpiContext = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto setContext = reinterpret_cast<SetDpiContext>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setContext) setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

bool CommandLineHasOption(const wchar_t* option) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    bool found = false;
    for (int i = 1; i < count; ++i) {
        if (_wcsicmp(arguments[i], option) == 0) found = true;
    }
    LocalFree(arguments);
    return found;
}

HANDLE AcquireInstanceMutex(bool& alreadyRunning) {
    // The Local namespace scopes this to the interactive Windows session.
    // Both language variants share the normal scope; isolated test/configuration
    // directories get their own scope without contacting an existing process.
    std::wstring scope = L"default";
    DWORD length = GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", nullptr, 0);
    if (length > 1) {
        std::wstring directory(length, L'\0');
        DWORD copied = GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", directory.data(), length);
        if (copied > 0 && copied < length) {
            directory.resize(copied);
            wchar_t fullPath[32768]{};
            DWORD fullLength = GetFullPathNameW(directory.c_str(),
                static_cast<DWORD>(std::size(fullPath)), fullPath, nullptr);
            scope = fullLength > 0 && fullLength < std::size(fullPath) ? fullPath : directory;
            std::replace(scope.begin(), scope.end(), L'/', L'\\');
            while (scope.size() > 3 && scope.back() == L'\\') scope.pop_back();
            CharLowerBuffW(scope.data(), static_cast<DWORD>(scope.size()));
        }
    }
    std::uint64_t hash = 14695981039346656037ULL;
    for (wchar_t character : scope) {
        hash ^= static_cast<std::uint16_t>(character);
        hash *= 1099511628211ULL;
    }
    wchar_t mutexName[96]{};
    swprintf_s(mutexName, L"Local\\WindowCue.Native.Singleton.%016llx", hash);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName);
    alreadyRunning = mutex && GetLastError() == ERROR_ALREADY_EXISTS;
    return mutex;
}

} // namespace
} // namespace windowcue

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using namespace windowcue;
    EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g.instance = instance;
    g.darkMode = LoadDarkModePreference();
    if (CommandLineHasOption(L"--startup-self-test")) {
        bool passed = RunStartupSelfTests();
        CoUninitialize();
        return passed ? 0 : 5;
    }
    if (CommandLineHasOption(L"--binding-self-test")) {
        constexpr wchar_t testClass[] = L"WindowCue.BindingIdentity.SelfTest";
        WNDCLASSW testWindowClass{};
        testWindowClass.lpfnWndProc = DefWindowProcW;
        testWindowClass.hInstance = instance;
        testWindowClass.lpszClassName = testClass;
        RegisterClassW(&testWindowClass);
        HWND first = CreateWindowExW(
            WS_EX_APPWINDOW, testClass, L"Duplicate test window",
            WS_POPUP | WS_CAPTION, -32000, -32000, 240, 160,
            nullptr, nullptr, instance, nullptr);
        HWND second = CreateWindowExW(
            WS_EX_APPWINDOW, testClass, L"Duplicate test window",
            WS_POPUP | WS_CAPTION, -32000, -32000, 240, 160,
            nullptr, nullptr, instance, nullptr);
        if (first) ShowWindow(first, SW_SHOWNOACTIVATE);
        if (second) ShowWindow(second, SW_SHOWNOACTIVATE);

        std::vector<WindowInfo> windows = EnumerateWindows(false, 0);
        const WindowInfo* target = nullptr;
        for (const WindowInfo& item : windows) {
            if (item.handle == second) {
                target = &item;
                break;
            }
        }
        bool passed = target != nullptr;
        if (target) {
            ActiveBinding binding;
            binding.target = target->handle;
            binding.settings.processPath = target->processPath;
            binding.settings.windowTitle = target->title;
            binding.settings.className = target->className;
            binding.settings.windowHandle = static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(target->handle));
            binding.settings.modifiers = MOD_ALT;
            binding.settings.virtualKey = VK_F24;
            binding.settings.hotkeyText = L"Alt + F24";
            passed = SaveSettings(std::vector<ActiveBinding>{binding});
            std::vector<BindingSettings> loaded = LoadSettings();
            passed = passed && loaded.size() == 1 &&
                     loaded[0].windowHandle == binding.settings.windowHandle &&
                     FindBestMatch(loaded[0], 0) == target->handle;
            if (!loaded.empty()) {
                BindingSettings ambiguousLegacy = loaded[0];
                ambiguousLegacy.windowHandle = 0;
                passed = passed && FindBestMatch(ambiguousLegacy, 0) == nullptr;
                loaded[0].windowHandle = std::numeric_limits<std::uint64_t>::max();
                passed = passed && FindBestMatch(loaded[0], 0) == nullptr;
            }
            binding.settings.isWindowCue = true;
            passed = passed && SaveSettings(std::vector<ActiveBinding>{binding});
            loaded = LoadSettings();
            passed = passed && loaded.size() == 1 && loaded[0].isWindowCue;
        }
        ReleaseWindows(windows);
        if (second) DestroyWindow(second);
        if (first) DestroyWindow(first);
        UnregisterClassW(testClass, instance);
        CoUninitialize();
        return passed ? 0 : 4;
    }
    if(CommandLineHasOption(L"--svg-self-test") || CommandLineHasOption(L"--render-self-test")) {
        // Preserve the old CLI alias for build scripts, validating the actual
        // vector geometry instead of the removed raster icon pipeline.
        RenderContext renderer;
        bool valid=renderer.ValidateGeometry() && ValidateThemePalettes();
        renderer.Shutdown();
        CoUninitialize();return valid ? 0 : 3;
    }
    bool alreadyRunning = false;
    HANDLE instanceMutex = AcquireInstanceMutex(alreadyRunning);
    if (!instanceMutex || alreadyRunning) {
        if (instanceMutex) CloseHandle(instanceMutex);
        CoUninitialize();
        return instanceMutex ? 0 : 1;
    }
    struct MutexLifetime {
        HANDLE handle;
        ~MutexLifetime() { CloseHandle(handle); }
    } mutexLifetime{instanceMutex};
    g.taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    g.appIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_WINDOWCUE), IMAGE_ICON, 32, 32, 0));
    g.appLargeIcon = static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_WINDOWCUE), IMAGE_ICON, 256, 256, 0));

    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance;
    windowClass.hIcon = g.appIcon;
    windowClass.hIconSm = g.appIcon;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&windowClass)) {
        if (g.appIcon) DestroyIcon(g.appIcon);
        CoUninitialize();
        return 1;
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    UINT initialDpi = QuerySystemDpi();
    int width = std::min(MulDiv(kNormalWindowWidth, static_cast<int>(initialDpi), 96),
                         RectWidth(work));
    int height = std::min(MulDiv(610, static_cast<int>(initialDpi), 96),
                          RectHeight(work));
    int left = work.left + (RectWidth(work) - width) / 2;
    int top = work.top + (RectHeight(work) - height) / 2;
    HWND hwnd = CreateWindowExW(
        WS_EX_APPWINDOW, kWindowClass, L"WindowCue",
        kMainWindowStyle,
        left, top, width, height, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        if (g.appIcon) DestroyIcon(g.appIcon);
        CoUninitialize();
        return 2;
    }

    AddTrayIcon();
    LoadBindings();
    InitializeStartupPreference();
    RefreshWindows(true);
    SetTimer(hwnd, kThumbnailTimer, 120, nullptr);
    SetTimer(hwnd, kRefreshTimer, 500, nullptr);
    // Normal and Windows-startup activation remain silent; the tray and Alt+P
    // expose the UI. --show is an explicit diagnostic/preview launch option.
    if (CommandLineHasOption(L"--show")) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (HandleSettingsMessage(message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
