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
constexpr int kSettingsWidth = 280;
constexpr int kTitleHeightDip = 40;
constexpr UINT_PTR kTrayRetryTimer = 3;
constexpr UINT_PTR kCaptionAnimationTimer = 4;
constexpr UINT_PTR kRenderRetryTimer = 5;
constexpr ULONGLONG kCaptionHoverDuration = 125;
constexpr UINT_PTR kSettingsTooltipId = 1;
constexpr UINT_PTR kThemeTooltipId = 2;
constexpr UINT_PTR kGitHubTooltipId = 3;
constexpr int kNormalWindowWidth = 1080;
// There is no system caption to hide or paint over. Keep only the behaviors
// needed by native resizing, system commands and Windows snap layouts.
constexpr DWORD kMainWindowStyle =
    WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;

struct Texts {
    const wchar_t* selectWindow;
    const wchar_t* bind;
    const wchar_t* unbind;
    const wchar_t* openSettings;
    const wchar_t* exit;
    const wchar_t* clickToRecord;
    const wchar_t* recordingHint;
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
        L"Select a window to bind a shortcut", L"Bind", L"Unbind", L"Open WindowCue", L"Exit",
        L"Click to record shortcut", L"Press a key combination...",
        L"Use Ctrl / Alt / Shift / Win", L"Record a shortcut with a modifier first",
        L" is already used by another app", L"No user windows found",
        L"Settings", L"Start with Windows",
        L"Disabled in Windows startup settings", L"Disabled by Windows policy",
        L"Startup is unavailable", L"Enable start with Windows", L"Disable start with Windows",
        L"Open Windows startup settings",
        L"GitHub repository", L"Could not open the link. Check your default browser.",
        L"Settings (Ctrl+,)", L"Switch to light mode", L"Switch to dark mode"};
#else
    static const Texts texts{
        L"选择一个窗口以绑定快捷键", L"绑定", L"取消绑定", L"打开 WindowCue", L"退出",
        L"点击录入快捷键", L"请按下组合键...",
        L"请同时使用 Ctrl / Alt / Shift / Win", L"请先录入包含修饰键的快捷键",
        L" 已被其他程序占用", L"没有找到用户窗口",
        L"设置", L"开机自启动",
        L"已在 Windows 启动设置中禁用", L"已被 Windows 策略禁用",
        L"开机自启动不可用", L"开启开机自启动", L"关闭开机自启动",
        L"打开 Windows 启动设置",
        L"GitHub 项目仓库", L"无法打开链接，请检查默认浏览器设置。",
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

    // Chrome & caption interaction
    int captionHot{};
    int captionPressed{};
    bool renderingFailed{};
    ChromeStatus chromeStatus;
    ChromeStatus settingsChromeStatus;
    unsigned renderRetries{};
    HWND captionTooltips{};

    // Single-HWND floating Settings popover state (zero child HWND swap-chains)
    HWND settingsWindow{};
    HWND settingsTooltips{};
    UINT settingsDpi{96};
    StartupStatus startupStatus;
    ToggleState lastStartupToggleState{ToggleState_Indeterminate};
    std::wstring settingsMessage;
    int settingsHoverItem{-1};    // 0 = startup row/switch, 1 = github, 2 = warning banner
    int settingsPressedItem{-1};
    int settingsFocusIndex{0};    // 0 = startup switch, 1 = github
    bool settingsFocusVisible{};
    bool settingsTrackingMouse{};
    bool settingsSuppressAutoClose{};
    bool settingsToggleClosed{};
    ULONGLONG settingsDismissedByGearTick{};
    RECT settingsStartupRowRect{};
    RECT settingsSwitchRect{};
    RECT settingsWarningRect{};
    RECT settingsDividerRect{};
    RECT settingsFooterRect{};
    RECT settingsGitHubRect{};

    // Tray & system
    UINT taskbarCreatedMessage{};
    int trayRetries{};
    NOTIFYICONDATAW tray{};
    HFONT titleFont{};
    HFONT bodyFont{};
    HFONT smallFont{};
    UINT dpi{96};

    // Window list & binding state
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
    bool scrollbarHovered{};
    bool hotkeyHovered{};
    bool bindButtonHovered{};
    bool bindButtonPressed{};
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

    // Main layout rects (physical pixels)
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
void LayoutSettingsPopover();
void ChangeStartupFromSettings();
void OpenSettingsLink(const wchar_t* link);

bool CanInteractWithStartup() {
    return g.startupStatus.state == StartupState::Enabled ||
           g.startupStatus.state == StartupState::Disabled ||
           g.startupStatus.state == StartupState::DisabledByUser;
}

ToggleState CurrentStartupToggleState() {
    if (g.startupStatus.state == StartupState::Enabled) return ToggleState_On;
    if (g.startupStatus.state == StartupState::Disabled) return ToggleState_Off;
    return ToggleState_Indeterminate;
}

class PopoverAccessibilityBridge final : public SettingsAccessibilityHost {
public:
    HWND PopoverHwnd() const override { return g.settingsWindow; }
    std::wstring ItemName(int id) const override {
        if (id == 0) {
            if (g.startupStatus.state == StartupState::DisabledByUser) return T().startupWindowsSettings;
            if (g.startupStatus.state == StartupState::Enabled) return T().startupDisable;
            return T().startupEnable;
        }
        return T().github;
    }
    RECT ItemScreenRect(int id) const override {
        RECT r = id == 0 ? g.settingsSwitchRect : g.settingsGitHubRect;
        if (g.settingsWindow && IsWindow(g.settingsWindow)) {
            MapWindowPoints(g.settingsWindow, nullptr, reinterpret_cast<POINT*>(&r), 2);
        }
        return r;
    }
    bool ItemEnabled(int id) const override {
        return id == 0 ? CanInteractWithStartup() : true;
    }
    int FocusedItem() const override { return g.settingsFocusIndex; }
    ToggleState StartupToggleState() const override { return CurrentStartupToggleState(); }
    void FocusItem(int id) override {
        if (id != 0 && id != 1) return;
        g.settingsFocusIndex = id;
        g.settingsFocusVisible = true;
        if (g.settingsWindow) {
            SetFocus(g.settingsWindow);
            InvalidateRect(g.settingsWindow, nullptr, FALSE);
        }
    }
    void InvokeItem(int id) override {
        if (id == 0) ChangeStartupFromSettings();
        else if (id == 1) OpenSettingsLink(kGitHubUrl);
    }
};

PopoverAccessibilityBridge gSettingsAccessibility;

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

int SettingsScale(int value) {
    return MulDiv(value, static_cast<int>(g.settingsDpi), 96);
}

RECT MakeRect(int left, int top, int right, int bottom) {
    return RECT{left, top, right, bottom};
}

int RectWidth(const RECT& rect) { return rect.right - rect.left; }
int RectHeight(const RECT& rect) { return rect.bottom - rect.top; }

bool PointInside(const RECT& rect, POINT point) {
    return PtInRect(&rect, point) != FALSE;
}

bool SameRect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top &&
           left.right == right.right && left.bottom == right.bottom;
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
    SendMessageW(g.captionTooltips, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
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

void CreateSettingsTooltip() {
    if (!g.settingsWindow || g.settingsTooltips) return;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&controls)) return;
    g.settingsTooltips = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        g.settingsWindow, nullptr, g.instance, nullptr);
    if (!g.settingsTooltips) return;
    SetWindowPos(g.settingsTooltips, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(g.settingsTooltips, TTM_SETDELAYTIME, TTDT_INITIAL, 450);
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    tool.hwnd = g.settingsWindow;
    tool.uId = kGitHubTooltipId;
    tool.rect = g.settingsGitHubRect;
    tool.lpszText = const_cast<wchar_t*>(T().github);
    SendMessageW(g.settingsTooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
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
    g.hotkeyHovered = g.bindButtonHovered = g.bindButtonPressed = false;
    g.scrollbarHovered = false;
    g.captionHot = g.captionPressed = 0;
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
    return GetEnvironmentVariableW(L"WINDOWCUE_RENDER_SOFTWARE", value, 2) == 1 && value[0] == L'1';
}

void DeleteThemeIcons() { g.renderer.Discard(); }
void RefreshThemeIcons() { g.renderer.Discard(); }

COLORREF BlendCaptionColor(COLORREF first, COLORREF second, double amount) {
    auto channel = [amount](BYTE a, BYTE b) {
        return static_cast<BYTE>(a + (b - a) * amount + 0.5);
    };
    return RGB(channel(GetRValue(first), GetRValue(second)),
               channel(GetGValue(first), GetGValue(second)),
               channel(GetBValue(first), GetBValue(second)));
}

void DrawUtilityButtonBackground(RenderContext& dc, const RECT& rect, double hover,
                                 bool pressed, bool selected) {
    if (hover <= 0.0 && !pressed && !selected) return;
    RECT pill = rect;
    InflateRect(&pill, -Scale(3), -Scale(6));
    COLORREF fill = pressed ? P().themePressed : P().themeHover;
    float alpha = (pressed || selected) ? 1.f : static_cast<float>(hover);
    dc.RoundedDip(pill, 6.f, fill, alpha);
}

void DrawCaptionButtons(RenderContext& dc) {
    const bool settingsOpen = g.settingsWindow && IsWindowVisible(g.settingsWindow);
    const bool settingsPressed = g.settingsButtonPressed && g.settingsButtonHovered;
    DrawUtilityButtonBackground(dc, g.settingsButtonRect, g.settingsHoverAnimation.value,
                                settingsPressed, settingsOpen);
    dc.Icon(VectorIcon::Settings, g.settingsButtonRect,
            settingsOpen ? P().selectedBorder : P().mainText, 16.f,
            settingsPressed ? 0.8f : 0.f);

    const bool themePressed = g.themeButtonPressed && g.themeButtonHovered;
    DrawUtilityButtonBackground(dc, g.themeButtonRect, g.themeHoverAnimation.value,
                                themePressed, false);
    dc.Icon(g.darkMode ? VectorIcon::Sun : VectorIcon::Moon, g.themeButtonRect,
            P().mainText, 16.f, themePressed ? 0.8f : 0.f);

    // Subtle Antigravity-style vertical hairline separating utility and window controls
    float dividerX = static_cast<float>(g.minButtonRect.left - Scale(6));
    dc.Line(dividerX, static_cast<float>(Scale(13)), dividerX, static_cast<float>(Scale(27)),
            1.f, P().divider);

    int id = 1;
    for (const RECT& r : {g.minButtonRect, g.maxButtonRect, g.closeButtonRect}) {
        const bool isHot = g.captionHot == id;
        const bool isPressed = g.captionPressed == id;
        if (isHot || isPressed) {
            if (id == 3) {
                // Flush top-right zone is cleanly anti-alias clipped by Win11 DWMWCP_ROUND.
                dc.Fill(r, isPressed ? P().closePressed : P().closeHover);
            } else {
                RECT pill = r;
                InflateRect(&pill, -Scale(4), -Scale(6));
                dc.RoundedDip(pill, 6.f, isPressed ? P().themePressed : P().themeHover);
            }
        }
        VectorIcon glyph = id == 1 ? VectorIcon::Minimize
            : (id == 2 ? (IsZoomed(g.hwnd) ? VectorIcon::Restore : VectorIcon::Maximize)
                       : VectorIcon::Close);
        COLORREF iconColor = (id == 3 && (isHot || isPressed)) ? RGB(255, 255, 255) : P().mainText;
        dc.Icon(glyph, r, iconColor, 10.5f);
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

void SaveBindings() { SaveSettings(g.bindings); }

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
    if (IsZoomed(g.hwnd) || g.chromeStatus.nativeBorder()) return;
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
        InvalidateCardDecoration(g.selected);
        InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
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
    InvalidateCardDecoration(g.selected);
    InvalidateRect(g.hwnd, &g.bindingRect, FALSE);
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
    int titleHeight{Scale(kTitleHeightDip)};
    int margin{Scale(12)};
    int bindingHeight{Scale(68)};
    int previewTopGap{Scale(4)};
    int bindingGap{Scale(10)};
    int cardWidth{Scale(194)};
    int cardHeight{Scale(152)};
    int cardGap{Scale(10)};
    int previewInset{Scale(10)};
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
    int captionWidth = Scale(44);
    int utilityWidth = Scale(34);
    int dividerSpace = Scale(12);

    g.titleRect = MakeRect(0, 0, client.right, titleHeight);
    g.closeButtonRect = MakeRect(client.right - captionWidth, 0, client.right, titleHeight);
    g.maxButtonRect = MakeRect(client.right - captionWidth * 2, 0,
                               client.right - captionWidth, titleHeight);
    g.minButtonRect = MakeRect(client.right - captionWidth * 3, 0,
                               client.right - captionWidth * 2, titleHeight);

    g.themeButtonRect = MakeRect(g.minButtonRect.left - dividerSpace - utilityWidth, 0,
                                 g.minButtonRect.left - dividerSpace, titleHeight);
    g.settingsButtonRect = MakeRect(g.themeButtonRect.left - utilityWidth, 0,
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

    int buttonWidth = Scale(90);
    int inputWidth = Scale(256);
    int controlHeight = Scale(38);
    int controlTop = g.bindingRect.top + (RectHeight(g.bindingRect) - controlHeight) / 2;
    g.bindButtonRect = MakeRect(g.bindingRect.right - Scale(14) - buttonWidth, controlTop,
                                g.bindingRect.right - Scale(14), controlTop + controlHeight);
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
        item.previewRect = MakeRect(left + Scale(8), top + Scale(44),
                                    left + cardWidth - Scale(8), top + cardHeight - Scale(8));
    }

    g.scrollbarRect = MakeRect(g.previewInner.right - Scale(7), g.previewInner.top,
                               g.previewInner.right, g.previewInner.bottom);
    g.scrollbarThumb = {};
    if (g.maxScroll > 0) {
        int trackHeight = RectHeight(g.scrollbarRect);
        int thumbHeight = std::max(Scale(30),
            trackHeight * trackHeight / (trackHeight + g.maxScroll));
        int travel = trackHeight - thumbHeight;
        int top = g.scrollbarRect.top +
            (g.maxScroll ? travel * g.scrollOffset / g.maxScroll : 0);
        g.scrollbarThumb = MakeRect(g.scrollbarRect.left + Scale(1), top,
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
    int frameHeight = std::max(0, RectHeight(window) - RectHeight(client));
    int desired = std::max(Scale(320),
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
        // Keep a 1px inset inside the rounded preview frame hairline.
        RECT inner = destination;
        InflateRect(&inner, -1, -1);
        if (RectWidth(inner) <= 0 || RectHeight(inner) <= 0) inner = destination;

        SIZE source{};
        if (FAILED(DwmQueryThumbnailSourceSize(item.thumbnail, &source)) ||
            source.cx <= 0 || source.cy <= 0) {
            SetThumbnailVisible(item, false);
            continue;
        }
        int hostWidth = RectWidth(inner);
        int hostHeight = RectHeight(inner);
        double scale = std::min(static_cast<double>(hostWidth) / source.cx,
                                static_cast<double>(hostHeight) / source.cy);
        int width = std::max(1, static_cast<int>(source.cx * scale + 0.5));
        int height = std::max(1, static_cast<int>(source.cy * scale + 0.5));
        int left = inner.left + (hostWidth - width) / 2;
        int top = inner.top + (hostHeight - height) / 2;
        DWM_THUMBNAIL_PROPERTIES properties{};
        properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_RECTSOURCE | DWM_TNP_OPACITY |
                             DWM_TNP_VISIBLE | DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination = MakeRect(left, top, left + width, top + height);
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

std::vector<std::wstring> SplitShortcutTokens(const std::wstring& chord) {
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < chord.size()) {
        size_t pos = chord.find(L" + ", start);
        if (pos == std::wstring::npos) {
            tokens.push_back(chord.substr(start));
            break;
        }
        tokens.push_back(chord.substr(start, pos - start));
        start = pos + 3;
    }
    return tokens;
}

void DrawHotkeyDisplay(RenderContext& dc, const RECT& area, bool hasSelection) {
    if (hasSelection && !g.inputError && g.captured && !g.inputText.empty()) {
        auto tokens = SplitShortcutTokens(g.inputText);
        if (!tokens.empty()) {
            const int pillPad = Scale(14);
            const int plusWidth = Scale(14);
            const int pillHeight = Scale(24);
            std::vector<int> widths;
            widths.reserve(tokens.size());
            int totalWidth = 0;
            for (size_t i = 0; i < tokens.size(); ++i) {
                int w = std::max(Scale(24), dc.MeasureRoleWidth(tokens[i], TextRole::Keycap, g.dpi) + pillPad);
                widths.push_back(w);
                totalWidth += w + (i > 0 ? plusWidth : 0);
            }
            if (totalWidth <= RectWidth(area)) {
                int x = area.left + (RectWidth(area) - totalWidth) / 2;
                int top = area.top + (RectHeight(area) - pillHeight) / 2;
                for (size_t i = 0; i < tokens.size(); ++i) {
                    if (i > 0) {
                        RECT plusRect = MakeRect(x, top, x + plusWidth, top + pillHeight);
                        dc.TextStyled(L"+", plusRect, TextRole::Small, P().mutedText,
                                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                        x += plusWidth;
                    }
                    RECT cap = MakeRect(x, top, x + widths[i], top + pillHeight);
                    dc.RoundedDip(cap, 6.f, P().keycapBackground);
                    dc.StrokeDip(cap, 6.f, 1.f, P().keycapBorder);
                    dc.TextStyled(tokens[i], cap, TextRole::Keycap, P().mainText,
                                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    x += widths[i];
                }
                return;
            }
        }
    }

    std::wstring display = g.inputText;
    COLORREF textColor = g.inputError ? P().errorBorder
        : (hasSelection ? P().mainText : P().placeholderText);
    if (display.empty()) {
        display = g.capturing ? T().recordingHint : T().clickToRecord;
        textColor = P().placeholderText;
    }
    dc.TextStyled(display, area, TextRole::Body, textColor,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void RenderInterfaceScene(RenderContext& dc, const RECT& client, const RECT& updateRect) {
    dc.Fill(client, P().windowBackground);

    // Title bar: brand icon + title + utility icons + divider + window controls
    int iconSize = Scale(18);
    int titleH = RectHeight(g.titleRect);
    int iconTop = (titleH - iconSize) / 2;
    dc.AppIcon(g.appIcon, MakeRect(Scale(12), iconTop, Scale(12) + iconSize, iconTop + iconSize));
    RECT appTitle = MakeRect(Scale(38), 0, g.settingsButtonRect.left - Scale(8), titleH);
    dc.TextStyled(L"WindowCue", appTitle, TextRole::Brand, P().titleText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawCaptionButtons(dc);

    // Hover animation stays within the caption. A newly recreated target still
    // needs the complete scene even when its first update was only the caption.
    if (dc.FreshFrame() || updateRect.bottom > g.titleRect.bottom) {
        // Recessed workspace well with generous Antigravity-grade 12 DIP radius
        dc.RoundedDip(g.previewOuter, 12.f, P().previewBackground);
        dc.StrokeDip(g.previewOuter, 12.f, 1.f, P().previewBorder);

        if (g.windows.empty()) {
            dc.TextStyled(T().noWindows, g.previewInner, TextRole::Body, P().mutedText,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        dc.PushClip(g.previewInner);
        for (size_t i = 0; i < g.windows.size(); ++i) {
            const WindowInfo& item = g.windows[i];
            RECT intersection{};
            if (!IntersectRect(&intersection, &item.cardRect, &g.previewInner)) continue;

            const bool isSelected = static_cast<int>(i) == g.selected;
            const bool isHovered = static_cast<int>(i) == g.hovered;
            const int cardBindingIdx = BindingIndexForWindow(item.handle);
            const bool isBound = cardBindingIdx >= 0;

            // Elevated card surface (10 DIP radius)
            dc.RoundedDip(item.cardRect, 10.f,
                          isHovered ? P().cardHoverBackground : P().cardBackground);

            COLORREF border = isSelected ? P().selectedBorder
                : (isBound ? P().boundBorder
                           : (isHovered ? P().hoverBorder : P().normalBorder));
            float borderDip = isSelected ? 2.f : (isBound ? 1.5f : 1.f);
            dc.StrokeDip(item.cardRect, 10.f, borderDip, border);

            // Preview well inside card (7 DIP radius)
            dc.RoundedDip(item.previewRect, 7.f, P().previewCanvas);
            dc.StrokeDip(item.previewRect, 7.f, 1.f, P().previewInnerBorder);

            // Card header: icon + title + process + optional shortcut pill badge
            int itemIcon = Scale(18);
            RECT iconBox = MakeRect(item.cardRect.left + Scale(10), item.cardRect.top + Scale(13),
                                    item.cardRect.left + Scale(10) + itemIcon,
                                    item.cardRect.top + Scale(13) + itemIcon);
            HICON displayIcon = item.isWindowCue ? g.appIcon : item.icon;
            if (displayIcon) {
                dc.AppIcon(displayIcon, iconBox);
            } else {
                dc.Icon(VectorIcon::WindowFallback, iconBox, P().mutedText, 16.f);
            }

            int titleRight = item.cardRect.right - Scale(10);
            if (isBound && !g.bindings[cardBindingIdx].settings.hotkeyText.empty()) {
                const std::wstring& hotkeyLabel = g.bindings[cardBindingIdx].settings.hotkeyText;
                int badgeW = std::clamp(
                    dc.MeasureRoleWidth(hotkeyLabel, TextRole::Badge, g.dpi) + Scale(12),
                    Scale(34), Scale(74));
                RECT badge = MakeRect(item.cardRect.right - Scale(8) - badgeW,
                                      item.cardRect.top + Scale(7),
                                      item.cardRect.right - Scale(8),
                                      item.cardRect.top + Scale(23));
                float badgeRad = RectHeight(badge) / (2.f * (static_cast<float>(g.dpi) / 96.f));
                dc.RoundedDip(badge, badgeRad, P().boundBadgeBackground);
                dc.StrokeDip(badge, badgeRad, 1.f, P().boundBadgeBorder);
                RECT badgeText = badge;
                InflateRect(&badgeText, -Scale(4), 0);
                dc.TextStyled(hotkeyLabel, badgeText, TextRole::Badge, P().boundBadgeText,
                              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                titleRight = badge.left - Scale(5);
            }

            RECT title = MakeRect(item.cardRect.left + Scale(35), item.cardRect.top + Scale(6),
                                  std::max<int>(item.cardRect.left + Scale(50), titleRight),
                                  item.cardRect.top + Scale(24));
            dc.TextStyled(item.title, title, TextRole::CardTitle, P().mainText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            RECT process = MakeRect(title.left, item.cardRect.top + Scale(23),
                                    item.cardRect.right - Scale(10), item.cardRect.top + Scale(38));
            dc.TextStyled(FileNameFromPath(item.processPath), process, TextRole::CardMeta,
                          P().processText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            // Self-card preview badge (cohesive elevated squircle with crisp 256px logo)
            if (item.isWindowCue && displayIcon) {
                int plateSize = Scale(64);
                int plateLeft = item.previewRect.left +
                    (RectWidth(item.previewRect) - plateSize) / 2;
                int plateTop = item.previewRect.top +
                    (RectHeight(item.previewRect) - plateSize) / 2;
                RECT plate = MakeRect(plateLeft, plateTop,
                                      plateLeft + plateSize, plateTop + plateSize);
                dc.RoundedDip(plate, 14.f, P().cardBackground);
                dc.StrokeDip(plate, 14.f, 1.f, P().normalBorder);

                int selfIconSize = Scale(44);
                int selfIconLeft = plateLeft + (plateSize - selfIconSize) / 2;
                int selfIconTop = plateTop + (plateSize - selfIconSize) / 2;
                dc.AppIcon(g.appLargeIcon ? g.appLargeIcon : displayIcon,
                           MakeRect(selfIconLeft, selfIconTop,
                                    selfIconLeft + selfIconSize, selfIconTop + selfIconSize));
            }
        }
        dc.PopClip();

        // Slim pill scrollbar
        if (g.maxScroll > 0) {
            dc.RoundedDip(g.scrollbarRect, 3.5f, P().scrollTrack);
            dc.RoundedDip(g.scrollbarThumb, 3.f,
                          (g.draggingScrollbar || g.scrollbarHovered)
                              ? P().scrollThumbHover : P().scrollThumb);
        }

        // Bottom binding dock (12 DIP radius)
        dc.RoundedDip(g.bindingRect, 12.f, P().panelBackground);
        dc.StrokeDip(g.bindingRect, 12.f, 1.f, P().panelBorder);

        const bool hasSelection = g.selected >= 0 && g.selected < static_cast<int>(g.windows.size());
        const int bindingIndex = hasSelection ? BindingIndexForWindow(g.windows[g.selected].handle) : -1;

        int wellSize = Scale(38);
        int wellX = g.bindingRect.left + Scale(14);
        int wellY = g.bindingRect.top + (RectHeight(g.bindingRect) - wellSize) / 2;
        RECT iconWell = MakeRect(wellX, wellY, wellX + wellSize, wellY + wellSize);
        dc.RoundedDip(iconWell, 10.f, P().iconWellBackground);
        dc.StrokeDip(iconWell, 10.f, 1.f, P().normalBorder);

        if (hasSelection) {
            const WindowInfo& item = g.windows[g.selected];
            int selectedIcon = Scale(22);
            int ix = wellX + (wellSize - selectedIcon) / 2;
            int iy = wellY + (wellSize - selectedIcon) / 2;
            HICON selectedWindowIcon = item.isWindowCue ? g.appIcon : item.icon;
            if (selectedWindowIcon) {
                dc.AppIcon(selectedWindowIcon, MakeRect(ix, iy, ix + selectedIcon, iy + selectedIcon));
            } else {
                dc.Icon(VectorIcon::WindowFallback, iconWell, P().mutedText, 18.f);
            }
            RECT selectedTitle = MakeRect(iconWell.right + Scale(12), g.bindingRect.top + Scale(13),
                                          g.hotkeyRect.left - Scale(14), g.bindingRect.top + Scale(34));
            dc.TextStyled(item.title, selectedTitle, TextRole::DockTitle, P().mainText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            RECT selectedPath = MakeRect(selectedTitle.left, g.bindingRect.top + Scale(35),
                                         selectedTitle.right, g.bindingRect.bottom - Scale(11));
            dc.TextStyled(item.processPath, selectedPath, TextRole::DockPath, P().mutedText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        } else {
            dc.Icon(VectorIcon::Keyboard, iconWell, P().mutedText, 18.f);
            RECT placeholder = MakeRect(iconWell.right + Scale(12), g.bindingRect.top,
                                        g.hotkeyRect.left - Scale(14), g.bindingRect.bottom);
            dc.TextStyled(T().selectWindow, placeholder, TextRole::Body, P().mutedText,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }

        // Hotkey recorder field (8 DIP radius) with recording indicator / keycaps
        COLORREF inputFill = g.inputError ? P().inputErrorBackground
            : (g.capturing ? P().inputCapturingBackground : P().inputBackground);
        COLORREF inputBorder = g.inputError ? P().errorBorder
            : (g.capturing ? P().selectedBorder
                           : ((g.hotkeyHovered && hasSelection && bindingIndex < 0)
                                  ? P().hoverBorder : P().inputBorder));
        float inputStroke = (g.inputError || g.capturing) ? 1.5f : 1.f;
        dc.RoundedDip(g.hotkeyRect, 8.f, inputFill);
        dc.StrokeDip(g.hotkeyRect, 8.f, inputStroke, inputBorder);

        RECT indicatorBox = MakeRect(g.hotkeyRect.left + Scale(8), g.hotkeyRect.top,
                                     g.hotkeyRect.left + Scale(28), g.hotkeyRect.bottom);
        if (g.capturing) {
            float cx = static_cast<float>(indicatorBox.left + RectWidth(indicatorBox) / 2);
            float cy = static_cast<float>(indicatorBox.top + RectHeight(indicatorBox) / 2);
            dc.Circle(cx, cy, static_cast<float>(Scale(7)), P().selectedBorder, 0.22f);
            dc.Circle(cx, cy, static_cast<float>(Scale(3)), P().selectedBorder, 1.f);
        } else {
            dc.Icon(VectorIcon::Keyboard, indicatorBox,
                    g.inputError ? P().errorBorder : P().mutedText, 15.f);
        }

        RECT inputContent = MakeRect(indicatorBox.right + Scale(2), g.hotkeyRect.top,
                                     g.hotkeyRect.right - Scale(10), g.hotkeyRect.bottom);
        DrawHotkeyDisplay(dc, inputContent, hasSelection);

        // Primary Bind / Unbind button (8 DIP radius) with crisp hover/press states
        COLORREF buttonColor = P().disabledButton;
        COLORREF buttonBorder = P().disabledButtonBorder;
        COLORREF buttonFg = P().disabledText;
        if (hasSelection) {
            if (bindingIndex >= 0) {
                buttonColor = g.bindButtonPressed ? P().unbindButtonPressed
                    : (g.bindButtonHovered ? P().unbindButtonHover : P().unbindButton);
                buttonBorder = P().unbindButtonBorder;
                buttonFg = P().unbindText;
            } else {
                buttonColor = g.bindButtonPressed ? P().bindButtonPressed
                    : (g.bindButtonHovered ? P().bindButtonHover : P().bindButton);
                buttonBorder = P().bindButtonBorder;
                buttonFg = P().buttonText;
            }
        }
        dc.RoundedDip(g.bindButtonRect, 8.f, buttonColor);
        dc.StrokeDip(g.bindButtonRect, 8.f, 1.f, buttonBorder);
        dc.TextStyled(bindingIndex >= 0 ? T().unbind : T().bind, g.bindButtonRect,
                      TextRole::BodyMedium, buttonFg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // On Windows 11, DWM draws the hardware rounded 1px outer frame border along
    // DWMWCP_ROUND. Only draw a client-side square border as a Windows 10 fallback.
    if (!IsZoomed(g.hwnd) && !g.chromeStatus.nativeBorder()) {
        dc.Stroke(client, 0.f, 1.f,
                  g.windowActive ? P().activeWindowBorder : P().inactiveWindowBorder);
    }
}

void DrawInterface(HDC, const RECT& updateRect, const std::vector<RECT>& region = {}) {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    auto& dc = g.renderer;
    if (!dc.Begin(g.hwnd, g.dpi, updateRect, UseSoftwareRenderer())) {
        g.renderingFailed = true;
        if (g.renderRetries < 3) SetTimer(g.hwnd, kRenderRetryTimer, 150, nullptr);
        return;
    }
    bool clipped = dc.PushRegion(region);
    RenderInterfaceScene(dc, client, updateRect);
    if (clipped) dc.PopRegion();
    HRESULT result = dc.End();
    g.renderingFailed = FAILED(result);
    if (SUCCEEDED(result)) {
        g.renderRetries = 0;
        KillTimer(g.hwnd, kRenderRetryTimer);
    }
    if (result == D2DERR_RECREATE_TARGET) InvalidateRect(g.hwnd, nullptr, FALSE);
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
    if (IsIconic(g.hwnd)) ShowWindow(g.hwnd, SW_RESTORE);
    RefreshWindows(true);
    RevealWindowWithoutFlash(g.hwnd, 0, 0, false, true);
    SetForegroundWindow(g.hwnd);
    UpdateThumbnails(true);
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
    COLORREF borderColor = (hwnd == g.hwnd && !g.windowActive)
        ? P().inactiveWindowBorder : P().activeWindowBorder;
    auto result = ConfigureOpaqueChrome(hwnd, borderColor, g.darkMode, true);
    if (hwnd == g.hwnd) g.chromeStatus = result;
    else g.settingsChromeStatus = result;
}

void ApplyDwmBorder(HWND hwnd) {
    if (!hwnd) return;
    COLORREF borderColor = (hwnd == g.hwnd && !g.windowActive)
        ? P().inactiveWindowBorder : P().activeWindowBorder;
    UpdateChromeBorderColor(hwnd, borderColor, true);
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
    ToggleState nextToggle = CurrentStartupToggleState();
    NotifySettingsToggleState(&gSettingsAccessibility, g.lastStartupToggleState, nextToggle);
    g.lastStartupToggleState = nextToggle;

    g.settingsMessage = StartupStateText(g.startupStatus.state);
    if (!g.settingsMessage.empty() && !g.startupStatus.detail.empty()) {
        g.settingsMessage += L"\r\n" + g.startupStatus.detail;
    }
    LayoutSettingsPopover();
    InvalidateRect(g.settingsWindow, nullptr, FALSE);
}

void RefreshStartupControls() {
    g.startupStatus = QueryStartupStatus();
    UpdateStartupControls();
}

void ApplySettingsTheme() {
    if (!g.settingsWindow) return;
    ApplyDwmFrame(g.settingsWindow);
    InvalidateRect(g.settingsWindow, nullptr, FALSE);
}

void LayoutSettingsPopover() {
    if (!g.settingsWindow) return;
    const int width = SettingsScale(kSettingsWidth);
    const int padX = SettingsScale(16);
    const int padTop = SettingsScale(14);
    const int padBottom = SettingsScale(12);
    const int innerW = width - padX * 2;

    int y = padTop;
    const int rowH = SettingsScale(32);
    g.settingsStartupRowRect = MakeRect(SettingsScale(8), y, width - SettingsScale(8), y + rowH);
    const int switchW = SettingsScale(38);
    const int switchH = SettingsScale(20);
    const int switchTop = y + (rowH - switchH) / 2;
    g.settingsSwitchRect = MakeRect(width - padX - switchW, switchTop,
                                    width - padX, switchTop + switchH);
    y += rowH;

    g.settingsWarningRect = {};
    if (!g.settingsMessage.empty()) {
        y += SettingsScale(8);
        int textW = innerW - SettingsScale(20);
        int textH = std::max(SettingsScale(18),
            g.settingsRenderer.MeasureRoleHeight(g.settingsMessage, TextRole::Small,
                                                 textW, g.settingsDpi));
        int boxH = textH + SettingsScale(14);
        g.settingsWarningRect = MakeRect(padX, y, width - padX, y + boxH);
        y += boxH;
    }

    y += SettingsScale(10);
    g.settingsDividerRect = MakeRect(padX, y, width - padX, y + 1);
    y += SettingsScale(10);

    const int footerH = SettingsScale(30);
    g.settingsFooterRect = MakeRect(padX, y, width - padX, y + footerH);
    const int ghSize = SettingsScale(30);
    g.settingsGitHubRect = MakeRect(width - padX - ghSize, y, width - padX, y + ghSize);
    y += footerH + padBottom;

    if (g.settingsTooltips) {
        TOOLINFOW tool{sizeof(tool)};
        tool.hwnd = g.settingsWindow;
        tool.uId = kGitHubTooltipId;
        tool.rect = g.settingsGitHubRect;
        SendMessageW(g.settingsTooltips, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }

    RECT client{};
    GetClientRect(g.settingsWindow, &client);
    if (RectWidth(client) != width || RectHeight(client) != y) {
        SetWindowPos(g.settingsWindow, nullptr, 0, 0, width, y,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void HideSettingsWindow() {
    if (!g.settingsWindow) return;
    if (g.settingsTooltips) SendMessageW(g.settingsTooltips, TTM_POP, 0, 0);
    g.settingsHoverItem = -1;
    g.settingsPressedItem = -1;
    g.settingsFocusVisible = false;
    if (IsWindowVisible(g.settingsWindow)) {
        ShowWindow(g.settingsWindow, SW_HIDE);
    }
    PopCaptionTooltip();
    InvalidateRect(g.hwnd, &g.settingsButtonRect, FALSE);
}

void OpenSettingsLink(const wchar_t* link) {
    HINSTANCE result = ShellExecuteW(g.settingsWindow, L"open", link,
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        g.settingsMessage = T().linkError;
        LayoutSettingsPopover();
        InvalidateRect(g.settingsWindow, nullptr, FALSE);
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

int HitSettingsItem(POINT pt) {
    if (PointInside(g.settingsSwitchRect, pt) || PointInside(g.settingsStartupRowRect, pt)) {
        return CanInteractWithStartup() ? 0 : -1;
    }
    if (PointInside(g.settingsGitHubRect, pt)) return 1;
    if (!g.settingsMessage.empty() &&
        g.startupStatus.state == StartupState::DisabledByUser &&
        PointInside(g.settingsWarningRect, pt)) {
        return 2;
    }
    return -1;
}

void RenderSettingsScene(RenderContext& dc, const RECT& r) {
    // Elevated floating popover surface
    dc.Fill(r, P().settingsSurface);

    // Row 1: Startup toggle row with gentle hover highlight
    const bool startupEnabled = CanInteractWithStartup();
    const bool startupOn = g.startupStatus.state == StartupState::Enabled;
    if (startupEnabled && (g.settingsHoverItem == 0 || g.settingsPressedItem == 0)) {
        dc.RoundedDip(g.settingsStartupRowRect, 8.f,
                      g.settingsPressedItem == 0 ? P().themePressed : P().themeHover, 0.72f);
    }

    RECT labelRect = MakeRect(SettingsScale(16), g.settingsStartupRowRect.top,
                              g.settingsSwitchRect.left - SettingsScale(10),
                              g.settingsStartupRowRect.bottom);
    dc.TextStyled(T().startup, labelRect, TextRole::Body,
                  startupEnabled ? P().mainText : P().disabledText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    // Pill toggle switch (38x20 DIP)
    COLORREF trackFill = startupOn ? P().switchOnTrack
        : (startupEnabled ? P().switchOffTrack : P().disabledButton);
    if (g.settingsPressedItem == 0 && startupEnabled) {
        trackFill = BlendCaptionColor(trackFill, P().mainText, 0.10);
    }
    dc.RoundedDip(g.settingsSwitchRect, 10.f, trackFill);
    if (!startupOn) {
        dc.StrokeDip(g.settingsSwitchRect, 10.f, 1.f,
                     startupEnabled ? P().normalBorder : P().disabledButtonBorder);
    }

    int knobSize = SettingsScale(14);
    int knobPad = SettingsScale(3);
    int knobLeft = startupOn ? (g.settingsSwitchRect.right - knobPad - knobSize)
                             : (g.settingsSwitchRect.left + knobPad);
    int knobTop = g.settingsSwitchRect.top + (RectHeight(g.settingsSwitchRect) - knobSize) / 2;
    RECT knob = MakeRect(knobLeft, knobTop, knobLeft + knobSize, knobTop + knobSize);
    dc.RoundedDip(knob, 7.f, startupEnabled ? P().switchThumb : P().mutedText);

    if (g.settingsFocusVisible && g.settingsFocusIndex == 0) {
        RECT focus = g.settingsSwitchRect;
        InflateRect(&focus, SettingsScale(3), SettingsScale(3));
        dc.StrokeDip(focus, 13.f, 1.5f, P().selectedBorder);
    }

    // Optional warning / status callout banner
    if (!g.settingsMessage.empty() && RectHeight(g.settingsWarningRect) > 0) {
        COLORREF bannerBg = (g.settingsHoverItem == 2)
            ? BlendCaptionColor(P().warningBackground, P().warningBorder, 0.14)
            : P().warningBackground;
        dc.RoundedDip(g.settingsWarningRect, 8.f, bannerBg);
        dc.StrokeDip(g.settingsWarningRect, 8.f, 1.f, P().warningBorder);
        RECT msgRect = g.settingsWarningRect;
        InflateRect(&msgRect, -SettingsScale(10), -SettingsScale(7));
        dc.TextStyled(g.settingsMessage, msgRect, TextRole::Small, P().warningText,
                      DT_LEFT | DT_WORDBREAK);
    }

    // Hairline divider
    dc.Line(static_cast<float>(g.settingsDividerRect.left),
            static_cast<float>(g.settingsDividerRect.top),
            static_cast<float>(g.settingsDividerRect.right),
            static_cast<float>(g.settingsDividerRect.top),
            1.f, P().divider);

    // Compact footer: brand label on left + crisp GitHub icon button on right
    RECT brandRect = MakeRect(g.settingsFooterRect.left, g.settingsFooterRect.top,
                              g.settingsGitHubRect.left - SettingsScale(8),
                              g.settingsFooterRect.bottom);
    dc.TextStyled(L"WindowCue", brandRect, TextRole::Small, P().mutedText,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (g.settingsHoverItem == 1 || g.settingsPressedItem == 1) {
        dc.RoundedDip(g.settingsGitHubRect, 7.f,
                      g.settingsPressedItem == 1 ? P().themePressed : P().themeHover);
    }
    dc.Icon(VectorIcon::GitHub, g.settingsGitHubRect, P().mainText, 18.f,
            g.settingsPressedItem == 1 ? 0.8f : 0.f);
    if (g.settingsFocusVisible && g.settingsFocusIndex == 1) {
        RECT focus = g.settingsGitHubRect;
        InflateRect(&focus, SettingsScale(2), SettingsScale(2));
        dc.StrokeDip(focus, 9.f, 1.5f, P().selectedBorder);
    }

    // Windows 10 fallback border when DWMWA_BORDER_COLOR is unavailable
    if (!g.settingsChromeStatus.nativeBorder()) {
        dc.Stroke(r, 0.f, 1.f, P().activeWindowBorder);
    }
}

void DrawSettingsPopover(HWND hwnd, const RECT& dirty) {
    RECT r{};
    GetClientRect(hwnd, &r);
    auto& dc = g.settingsRenderer;
    if (!dc.Begin(hwnd, g.settingsDpi, dirty, UseSoftwareRenderer())) return;
    RenderSettingsScene(dc, r);
    if (dc.End() == D2DERR_RECREATE_TARGET) {
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK SettingsProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCPAINT:
        return 0;
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd, message, wParam, -1);
    case WM_GETOBJECT:
        if (static_cast<LONG>(lParam) == UiaRootObjectId) {
            return SettingsPopoverAutomationObject(hwnd, wParam, lParam, &gSettingsAccessibility);
        }
        break;
    case WM_CREATE:
        g.settingsWindow = hwnd;
        g.settingsDpi = QueryWindowDpi(hwnd);
        ApplySettingsTheme();
        RefreshStartupControls();
        LayoutSettingsPopover();
        CreateSettingsTooltip();
        return 0;
    case WM_DPICHANGED: {
        g.settingsDpi = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     RectWidth(*suggested), RectHeight(*suggested),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutSettingsPopover();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            if (!g.settingsSuppressAutoClose && IsWindowVisible(hwnd)) {
                // If deactivated because the user clicked the gear button on the
                // main window, remember so the gear click toggles cleanly closed.
                POINT cursor{};
                if (GetCursorPos(&cursor) && g.hwnd) {
                    POINT clientPt = cursor;
                    ScreenToClient(g.hwnd, &clientPt);
                    if (PointInside(g.settingsButtonRect, clientPt) &&
                        (GetKeyState(VK_LBUTTON) & 0x8000) != 0) {
                        g.settingsDismissedByGearTick = GetTickCount64();
                    }
                }
                HideSettingsWindow();
            }
        } else {
            RefreshStartupControls();
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hot = HitSettingsItem(pt);
        if (hot != g.settingsHoverItem) {
            g.settingsHoverItem = hot;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (!g.settingsTrackingMouse) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            g.settingsTrackingMouse = true;
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g.settingsTrackingMouse = false;
        if (g.settingsHoverItem != -1) {
            g.settingsHoverItem = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int hit = HitSettingsItem(pt);
        g.settingsFocusVisible = false;
        if (hit >= 0) {
            g.settingsPressedItem = hit;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        int pressed = g.settingsPressedItem;
        g.settingsPressedItem = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        int hit = HitSettingsItem(pt);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (pressed >= 0 && hit == pressed) {
            if (pressed == 0 || pressed == 2) ChangeStartupFromSettings();
            else if (pressed == 1) OpenSettingsLink(kGitHubUrl);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        g.settingsPressedItem = -1;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(nullptr, HitSettingsItem(pt) >= 0 ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HideSettingsWindow();
            SetFocus(g.hwnd);
            return 0;
        }
        if (wParam == VK_TAB) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            g.settingsFocusVisible = true;
            if (CanInteractWithStartup()) {
                g.settingsFocusIndex = shift
                    ? (g.settingsFocusIndex == 0 ? 1 : 0)
                    : (g.settingsFocusIndex == 0 ? 1 : 0);
            } else {
                g.settingsFocusIndex = 1;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            g.settingsFocusVisible = true;
            if (g.settingsFocusIndex == 0 && CanInteractWithStartup()) {
                ChangeStartupFromSettings();
            } else if (g.settingsFocusIndex == 1) {
                OpenSettingsLink(kGitHubUrl);
            }
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT: {
        HDC printDc = reinterpret_cast<HDC>(wParam);
        if (!printDc) return 0;
        RECT r{};
        GetClientRect(hwnd, &r);
        RenderContext printRenderer;
        if (printRenderer.BeginDC(printDc, g.settingsDpi, r)) {
            RenderSettingsScene(printRenderer, r);
            printRenderer.End();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        DrawSettingsPopover(hwnd, paint.rcPaint);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        HideSettingsWindow();
        SetFocus(g.hwnd);
        return 0;
    case WM_DESTROY:
        if (g.settingsTooltips) DestroyWindow(g.settingsTooltips);
        g.settingsTooltips = nullptr;
        g.settingsRenderer.Shutdown();
        g.settingsWindow = nullptr;
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
        POINT initialAnchor{g.settingsButtonRect.right, g.settingsButtonRect.bottom};
        ClientToScreen(g.hwnd, &initialAnchor);
        int initW = Scale(kSettingsWidth);
        int initH = Scale(112);
        // Create on the owner's monitor so initial layout uses its DPI.
        CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass, T().settings, WS_POPUP,
                        initialAnchor.x - initW, initialAnchor.y + Scale(6),
                        initW, initH, g.hwnd, nullptr, g.instance, nullptr);
    }
    if (!g.settingsWindow) return;

    g.settingsDpi = QueryWindowDpi(g.hwnd);
    g.settingsHoverItem = -1;
    g.settingsPressedItem = -1;
    g.settingsFocusIndex = CanInteractWithStartup() ? 0 : 1;
    g.settingsFocusVisible = false;
    ApplySettingsTheme();
    RefreshStartupControls();
    LayoutSettingsPopover();

    RECT bounds{};
    GetWindowRect(g.settingsWindow, &bounds);
    POINT anchor{g.settingsButtonRect.right, g.settingsButtonRect.bottom + Scale(6)};
    ClientToScreen(g.hwnd, &anchor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(g.hwnd, MONITOR_DEFAULTTONEAREST), &monitor);
    int left = std::clamp(anchor.x - RectWidth(bounds), monitor.rcWork.left,
                          std::max(monitor.rcWork.left, monitor.rcWork.right - RectWidth(bounds)));
    int top = std::clamp(anchor.y, monitor.rcWork.top,
                         std::max(monitor.rcWork.top, monitor.rcWork.bottom - RectHeight(bounds)));

    // Pre-paint while cloaked so DWM presents the fully rendered popover on
    // frame 0 with zero black-screen flash.
    g.settingsSuppressAutoClose = true;
    RevealWindowWithoutFlash(g.settingsWindow, left, top, true, true);
    SetForegroundWindow(g.settingsWindow);
    SetFocus(g.settingsWindow);
    g.settingsSuppressAutoClose = false;
    InvalidateRect(g.hwnd, &g.settingsButtonRect, FALSE);
}

bool HandleSettingsMessage(MSG& message) {
    if (!g.settingsWindow || !IsWindowVisible(g.settingsWindow) ||
        message.hwnd != g.settingsWindow) {
        return false;
    }
    if (message.message == WM_KEYDOWN &&
        (message.wParam == VK_ESCAPE || message.wParam == VK_TAB ||
         message.wParam == VK_RETURN || message.wParam == VK_SPACE)) {
        SendMessageW(g.settingsWindow, WM_KEYDOWN, message.wParam, message.lParam);
        return true;
    }
    return false;
}

void ToggleTheme() {
    PopCaptionTooltip();
    g.darkMode = !g.darkMode;
    UpdateCaptionTooltipText();
    SaveDarkModePreference(g.darkMode);
    ApplyDwmFrame(g.hwnd);
    ApplySettingsTheme();
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

int CaptionButtonAt(POINT client) {
    return PointInside(g.minButtonRect, client) ? 1
         : PointInside(g.maxButtonRect, client) ? 2
         : PointInside(g.closeButtonRect, client) ? 3 : 0;
}

void InvokeCaptionButton(int id) {
    if (id == 1) SendMessageW(g.hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    if (id == 2) SendMessageW(g.hwnd, WM_SYSCOMMAND, IsZoomed(g.hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
    if (id == 3) SendMessageW(g.hwnd, WM_CLOSE, 0, 0);
}

LRESULT HitTest(POINT screenPoint) {
    POINT client = screenPoint;
    ScreenToClient(g.hwnd, &client);
    LRESULT border = ResizeBorderHit(g.hwnd, g.dpi, client);
    if (border != HTNOWHERE) return border;
    int button = CaptionButtonAt(client);
    if (button) return button == 1 ? HTMINBUTTON : button == 2 ? HTMAXBUTTON : HTCLOSE;
    if (PointInside(g.themeButtonRect, client) ||
        PointInside(g.settingsButtonRect, client)) {
        return HTCLIENT;
    }
    if (client.y < Scale(kTitleHeightDip)) return HTCAPTION;
    return HTCLIENT;
}

bool IsInteractiveClientPoint(POINT client) {
    if (PointInside(g.settingsButtonRect, client) || PointInside(g.themeButtonRect, client)) {
        return true;
    }
    if (HitCard(client) >= 0) return true;
    bool hasSelection = g.selected >= 0 && g.selected < static_cast<int>(g.windows.size());
    if (hasSelection && PointInside(g.bindButtonRect, client)) return true;
    if (hasSelection && PointInside(g.hotkeyRect, client) &&
        BindingIndexForWindow(g.windows[g.selected].handle) < 0) {
        return true;
    }
    return false;
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
        return DefWindowProcW(hwnd, message, wParam, -1);
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return HitTest(point);
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = Scale(760);
        info->ptMinTrackSize.y = Scale(320);
        SetMaximizedWorkArea(hwnd, *info);
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
        if (wParam != SIZE_MINIMIZED) {
            ApplyDwmBorder(hwnd);
            ResizeForContent();
        }
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
        ApplyDwmFrame(hwnd);
        ApplySettingsTheme();
        InvalidateRect(hwnd, nullptr, FALSE);
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
    case WM_PRINTCLIENT: {
        HDC printDc = reinterpret_cast<HDC>(wParam);
        if (!printDc) return 0;
        RECT client{};
        GetClientRect(hwnd, &client);
        Layout();
        RenderContext printRenderer;
        if (printRenderer.BeginDC(printDc, g.dpi, client)) {
            RenderInterfaceScene(printRenderer, client, client);
            printRenderer.End();
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto region = WindowPaintRegion(hwnd);
        HDC dc = BeginPaint(hwnd, &paint);
        Layout();
        DrawInterface(dc, paint.rcPaint, region);
        EndPaint(hwnd, &paint);
        UpdateThumbnails();
        return 0;
    }
    case WM_GETTITLEBARINFOEX: {
        auto* info = reinterpret_cast<TITLEBARINFOEX*>(lParam);
        if (!info || info->cbSize != sizeof(*info)) return 0;
        info->rcTitleBar = g.titleRect;
        MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&info->rcTitleBar), 2);
        for (int i = 0; i < 6; ++i) {
            info->rgstate[i] = STATE_SYSTEM_INVISIBLE;
            info->rgrect[i] = {};
        }
        info->rgstate[0] = 0;
        int indexes[] = {2, 3, 5};
        RECT rects[] = {g.minButtonRect, g.maxButtonRect, g.closeButtonRect};
        for (int i = 0; i < 3; ++i) {
            info->rgstate[indexes[i]] = STATE_SYSTEM_FOCUSABLE;
            info->rgrect[indexes[i]] = rects[i];
            MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&info->rgrect[indexes[i]]), 2);
        }
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            SetCursor(LoadCursorW(nullptr, IsInteractiveClientPoint(pt) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_NCMOUSEMOVE: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &p);
        int hot = CaptionButtonAt(p);
        if (hot != g.captionHot) {
            g.captionHot = hot;
            InvalidateRect(hwnd, &g.titleRect, FALSE);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE | TME_NONCLIENT, hwnd, 0};
        TrackMouseEvent(&tracking);
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    case WM_NCMOUSELEAVE:
        g.captionHot = 0;
        InvalidateRect(hwnd, &g.titleRect, FALSE);
        return 0;
    case WM_NCLBUTTONDOWN: {
        if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) HideSettingsWindow();
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &p);
        int id = CaptionButtonAt(p);
        if (id) {
            g.captionPressed = id;
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.titleRect, FALSE);
            return 0;
        }
        break;
    }
    case WM_NCLBUTTONUP: {
        POINT p{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd, &p);
        int id = CaptionButtonAt(p), pressed = g.captionPressed;
        g.captionPressed = 0;
        ReleaseCapture();
        if (id && id == pressed) InvokeCaptionButton(id);
        InvalidateRect(hwnd, &g.titleRect, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g.captionPressed) {
            g.captionHot = CaptionButtonAt(point);
            InvalidateRect(hwnd, &g.titleRect, FALSE);
            return 0;
        }
        if (g.captionHot) {
            g.captionHot = 0;
            InvalidateRect(hwnd, &g.titleRect, FALSE);
        }
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
        bool scrollHot = g.maxScroll > 0 && PointInside(g.scrollbarRect, point);
        if (scrollHot != g.scrollbarHovered) {
            g.scrollbarHovered = scrollHot;
            InvalidateRect(hwnd, &g.scrollbarRect, FALSE);
        }
        bool dockHotkeyHot = PointInside(g.hotkeyRect, point);
        bool dockBindHot = PointInside(g.bindButtonRect, point);
        if (dockHotkeyHot != g.hotkeyHovered || dockBindHot != g.bindButtonHovered) {
            g.hotkeyHovered = dockHotkeyHot;
            g.bindButtonHovered = dockBindHot;
            InvalidateRect(hwnd, &g.bindingRect, FALSE);
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
        if (g.scrollbarHovered) {
            g.scrollbarHovered = false;
            InvalidateRect(hwnd, &g.scrollbarRect, FALSE);
        }
        if (g.hotkeyHovered || g.bindButtonHovered) {
            g.hotkeyHovered = g.bindButtonHovered = false;
            InvalidateRect(hwnd, &g.bindingRect, FALSE);
        }
        SetCaptionHovered(false, false);
        SetCaptionHovered(true, false);
        PopCaptionTooltip();
        return 0;
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointInside(g.settingsButtonRect, point)) {
            ULONGLONG now = GetTickCount64();
            bool justDismissed = g.settingsDismissedByGearTick != 0 &&
                (now - g.settingsDismissedByGearTick) < 350;
            g.settingsDismissedByGearTick = 0;
            g.settingsToggleClosed = justDismissed ||
                (g.settingsWindow && IsWindowVisible(g.settingsWindow));
            if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) {
                HideSettingsWindow();
            }
            g.settingsButtonPressed = true;
            SetCaptionHovered(true, true);
            PopCaptionTooltip();
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
            return 0;
        }
        // Clicking anywhere in the main business area automatically closes Settings.
        if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) {
            HideSettingsWindow();
        }
        if (PointInside(g.themeButtonRect, point)) {
            g.themeButtonPressed = true;
            SetCaptionHovered(false, true);
            PopCaptionTooltip();
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
            return 0;
        }
        bool hasSelection = g.selected >= 0 && g.selected < static_cast<int>(g.windows.size());
        if (hasSelection && PointInside(g.bindButtonRect, point)) {
            g.bindButtonPressed = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.bindingRect, FALSE);
            return 0;
        }
        if (g.maxScroll > 0 && PointInside(g.scrollbarThumb, point)) {
            g.draggingScrollbar = true;
            g.scrollbarDragY = point.y;
            g.scrollbarStartOffset = g.scrollOffset;
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.scrollbarRect, FALSE);
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
    case WM_RBUTTONDOWN:
        if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) HideSettingsWindow();
        break;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (g.captionPressed) {
            int id = g.captionPressed;
            g.captionPressed = 0;
            ReleaseCapture();
            if (CaptionButtonAt(point) == id) InvokeCaptionButton(id);
            InvalidateRect(hwnd, &g.titleRect, FALSE);
            return 0;
        }
        if (g.settingsButtonPressed) {
            bool inside = PointInside(g.settingsButtonRect, point);
            bool wasClosing = g.settingsToggleClosed;
            g.settingsButtonPressed = false;
            g.settingsToggleClosed = false;
            SetCaptionHovered(true, inside);
            ReleaseCapture();
            if (inside && !wasClosing) {
                ShowSettingsWindow();
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
        if (g.bindButtonPressed) {
            bool clicked = PointInside(g.bindButtonRect, point);
            g.bindButtonPressed = false;
            ReleaseCapture();
            if (clicked) BindOrUnbindSelected();
            else InvalidateRect(hwnd, &g.bindingRect, FALSE);
            return 0;
        }
        if (g.draggingScrollbar) {
            g.draggingScrollbar = false;
            ReleaseCapture();
            InvalidateRect(hwnd, &g.scrollbarRect, FALSE);
            return 0;
        }
        HandleClick(point);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (g.captionPressed) InvalidateRect(hwnd, &g.titleRect, FALSE);
        g.captionPressed = 0;
        g.draggingScrollbar = false;
        if (g.bindButtonPressed) {
            g.bindButtonPressed = false;
            InvalidateRect(hwnd, &g.bindingRect, FALSE);
        }
        if (g.settingsButtonPressed) InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
        if (g.themeButtonPressed) InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
        g.settingsButtonPressed = false;
        g.settingsToggleClosed = false;
        g.themeButtonPressed = false;
        return 0;
    case WM_MOUSEWHEEL: {
        if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) HideSettingsWindow();
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
            if (g.settingsWindow && IsWindowVisible(g.settingsWindow)) HideSettingsWindow();
            else ShowSettingsWindow();
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
        ApplyDwmBorder(hwnd);
        InvalidateWindowChrome();
        if (wasCapturing) InvalidateRect(hwnd, &g.bindingRect, FALSE);
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
        if (wParam == kRenderRetryTimer) {
            KillTimer(hwnd, kRenderRetryTimer);
            ++g.renderRetries;
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
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
        } else if (wParam == kThumbnailTimer) {
            UpdateThumbnails();
        } else if (wParam == kRefreshTimer) {
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
        SharedFactories().Reset();
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
    if (CommandLineHasOption(L"--svg-self-test") || CommandLineHasOption(L"--render-self-test")) {
        RenderContext renderer;
        bool valid = renderer.ValidateGeometry() && ValidateThemePalettes();
        renderer.Shutdown();
        SharedFactories().Reset();
        CoUninitialize();
        return valid ? 0 : 3;
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

    if (CommandLineHasOption(L"--show")) {
        RevealWindowWithoutFlash(hwnd, 0, 0, false, true);
        UpdateThumbnails(true);
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
