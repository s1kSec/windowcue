#include "WindowCueNative.h"
#include "../resource.h"

#include <objbase.h>
#include <uxtheme.h>
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
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "uxtheme.lib")

namespace windowcue {
namespace {

constexpr wchar_t kWindowClass[] = L"WindowCue.Native.Window";
constexpr wchar_t kSettingsClass[] = L"WindowCue.Native.Settings";
constexpr wchar_t kGitHubUrl[] = L"https://github.com/s1kSec/windowcue";
constexpr int kStartupButtonId = 301;
constexpr int kGitHubButtonId = 302;
constexpr UINT_PTR kTrayRetryTimer = 3;
constexpr int kNormalWindowWidth = 1080;
constexpr DWORD kDwmUseImmersiveDarkMode = 20;
constexpr DWORD kDwmWindowAttributeCaptionButtonBounds = 5;
constexpr DWORD kDwmWindowAttributeBorderColor = 34;
constexpr DWORD kDwmWindowAttributeCaptionColor = 35;
constexpr DWORD kDwmWindowAttributeTextColor = 36;

struct ThemePalette {
    COLORREF windowBackground;
    COLORREF panelBackground;
    COLORREF cardBackground;
    COLORREF normalBorder;
    COLORREF hoverBorder;
    COLORREF selectedBorder;
    COLORREF boundBorder;
    COLORREF mutedText;
    COLORREF mainText;
    COLORREF inputBackground;
    COLORREF errorBorder;
    COLORREF activeWindowBorder;
    COLORREF inactiveWindowBorder;
    COLORREF titleText;
    COLORREF previewBackground;
    COLORREF previewBorder;
    COLORREF previewCanvas;
    COLORREF processText;
    COLORREF scrollTrack;
    COLORREF scrollThumb;
    COLORREF panelBorder;
    COLORREF inputBorder;
    COLORREF placeholderText;
    COLORREF disabledText;
    COLORREF disabledButton;
    COLORREF disabledButtonBorder;
    COLORREF unbindButton;
    COLORREF unbindButtonBorder;
    COLORREF bindButton;
    COLORREF bindButtonBorder;
    COLORREF themeHover;
    COLORREF themePressed;
    COLORREF buttonText;
};

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
    const wchar_t* startupOn;
    const wchar_t* startupOff;
    const wchar_t* startupWindowsDisabled;
    const wchar_t* startupPolicyDisabled;
    const wchar_t* startupUnavailable;
    const wchar_t* startupEnable;
    const wchar_t* startupDisable;
    const wchar_t* startupWindowsSettings;
    const wchar_t* startupHint;
    const wchar_t* github;
    const wchar_t* linkError;
};

const Texts& T() {
#ifdef LANG_EN
    static const Texts texts{
        L"Select a window first", L"Bind", L"Unbind", L"Open settings", L"Exit",
        L"Click and press a shortcut", L"Use Ctrl / Alt / Shift / Win",
        L"Record a shortcut with a modifier first", L" is already used by another app",
        L"No user windows found", L"Settings", L"Start with Windows", L"On", L"Off",
        L"Disabled in Windows startup settings", L"Disabled by Windows policy",
        L"Startup is unavailable", L"Enable start with Windows", L"Disable start with Windows",
        L"Open Windows startup settings", L"WindowCue starts quietly in the system tray.",
        L"GitHub project", L"Could not open the link. Check your default browser."};
#else
    static const Texts texts{
        L"请先选择一个窗口", L"绑定", L"取消绑定", L"打开设置", L"退出",
        L"点击后按快捷键", L"请同时使用 Ctrl / Alt / Shift / Win",
        L"请先录入包含修饰键的快捷键", L" 已被其他程序占用",
        L"没有找到用户窗口", L"设置", L"开机自启动", L"开启", L"关闭",
        L"已在 Windows 启动设置中禁用", L"已被 Windows 策略禁用",
        L"开机自启动不可用", L"开启开机自启动", L"关闭开机自启动",
        L"打开 Windows 启动设置", L"WindowCue 启动后静默驻留系统托盘。",
        L"GitHub 项目", L"无法打开链接，请检查默认浏览器设置。"};
#endif
    return texts;
}

struct AppState {
    HINSTANCE instance{};
    HWND hwnd{};
    HICON appIcon{};
    HBITMAP sunIcon{};
    HBITMAP moonIcon{};
    HBITMAP settingsDarkIcon{};
    HBITMAP settingsLightIcon{};
    HWND settingsWindow{};
    HWND startupButton{};
    HWND githubButton{};
    HWND startupLabel{};
    HWND startupStatusLabel{};
    HWND startupHintLabel{};
    HFONT settingsFont{};
    HBRUSH settingsBackgroundBrush{};
    HBITMAP githubDarkIcon{};
    HBITMAP githubLightIcon{};
    UINT settingsDpi{96};
    UINT taskbarCreatedMessage{};
    int trayRetries{};
    StartupStatus startupStatus;
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

const ThemePalette& P() {
    static const ThemePalette dark{
        RGB(40, 42, 48), RGB(33, 33, 33), RGB(23, 23, 23),
        RGB(58, 58, 58), RGB(94, 94, 94), RGB(16, 163, 127),
        RGB(126, 211, 162), RGB(180, 180, 180), RGB(236, 236, 236),
        RGB(24, 24, 24), RGB(239, 107, 107), RGB(82, 84, 90),
        RGB(58, 60, 66), RGB(236, 236, 236), RGB(33, 33, 33),
        RGB(58, 58, 58), RGB(0, 0, 0), RGB(166, 166, 166),
        RGB(43, 43, 43), RGB(104, 104, 104), RGB(58, 58, 58),
        RGB(86, 86, 86), RGB(155, 155, 155), RGB(160, 160, 160),
        RGB(52, 52, 52), RGB(75, 75, 75), RGB(127, 50, 50),
        RGB(198, 93, 93), RGB(236, 236, 236), RGB(236, 236, 236),
        RGB(52, 52, 52), RGB(65, 65, 65), RGB(23, 23, 23)
    };
    static const ThemePalette light{
        RGB(245, 248, 252), RGB(238, 243, 249), RGB(245, 248, 252),
        RGB(211, 213, 216), RGB(157, 160, 164), RGB(15, 143, 112),
        RGB(22, 135, 94), RGB(91, 95, 101), RGB(35, 36, 38),
        RGB(245, 248, 252), RGB(201, 63, 63), RGB(188, 190, 194),
        RGB(211, 213, 216), RGB(47, 48, 51), RGB(238, 243, 249),
        RGB(223, 224, 226), RGB(233, 238, 245), RGB(101, 105, 111),
        RGB(232, 233, 235), RGB(168, 170, 174), RGB(218, 219, 222),
        RGB(176, 178, 182), RGB(105, 109, 115), RGB(96, 98, 102),
        RGB(230, 231, 233), RGB(207, 209, 212), RGB(179, 38, 30),
        RGB(140, 29, 24), RGB(35, 36, 38), RGB(35, 36, 38),
        RGB(226, 227, 229), RGB(214, 215, 218), RGB(245, 248, 252)
    };
    return g.darkMode ? dark : light;
}

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

void DeleteFonts() {
    if (g.titleFont) DeleteObject(g.titleFont);
    if (g.bodyFont) DeleteObject(g.bodyFont);
    if (g.smallFont) DeleteObject(g.smallFont);
    g.titleFont = g.bodyFont = g.smallFont = nullptr;
}

HFONT CreateUiFont(int pointSize, int weight) {
    HDC dc = GetDC(g.hwnd);
    int logicalHeight = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
    ReleaseDC(g.hwnd, dc);
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

void FillRounded(HDC dc, const RECT& rect, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_NULL, 0, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void StrokeRounded(HDC dc, const RECT& rect, int radius, int width, COLORREF color) {
    HBRUSH brush = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}

void DrawTextValue(HDC dc, const std::wstring& text, RECT rect, HFONT font,
                   COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}

void DeleteThemeIcons() {
    if (g.sunIcon) DeleteObject(g.sunIcon);
    if (g.moonIcon) DeleteObject(g.moonIcon);
    if (g.settingsDarkIcon) DeleteObject(g.settingsDarkIcon);
    if (g.settingsLightIcon) DeleteObject(g.settingsLightIcon);
    g.sunIcon = g.moonIcon = nullptr;
    g.settingsDarkIcon = g.settingsLightIcon = nullptr;
}

void RefreshThemeIcons() {
    DeleteThemeIcons();
    int iconSize = Scale(18);
    g.sunIcon = RenderSvgResource(g.instance, IDR_SUN_SVG, iconSize);
    g.moonIcon = RenderSvgResource(g.instance, IDR_MOON_SVG, iconSize);
    g.settingsDarkIcon = RenderSvgResource(g.instance, IDR_SETTINGS_DARK_SVG, iconSize);
    g.settingsLightIcon = RenderSvgResource(g.instance, IDR_SETTINGS_LIGHT_SVG, iconSize);
}

void DrawFallbackThemeIcon(HDC dc, const RECT& rect, bool sun) {
    int size = Scale(18);
    int left = rect.left + (RectWidth(rect) - size) / 2;
    int top = rect.top + (RectHeight(rect) - size) / 2;
    COLORREF color = P().mainText;
    HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(1)), color);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (sun) {
        int centerX = left + size / 2;
        int centerY = top + size / 2;
        int radius = Scale(3);
        Ellipse(dc, centerX - radius, centerY - radius,
                centerX + radius + 1, centerY + radius + 1);
        const POINT rays[][2] = {
            {{centerX, top}, {centerX, top + Scale(3)}},
            {{centerX, top + size - Scale(3)}, {centerX, top + size}},
            {{left, centerY}, {left + Scale(3), centerY}},
            {{left + size - Scale(3), centerY}, {left + size, centerY}},
            {{left + Scale(2), top + Scale(2)}, {left + Scale(4), top + Scale(4)}},
            {{left + size - Scale(4), top + size - Scale(4)},
             {left + size - Scale(2), top + size - Scale(2)}},
            {{left + size - Scale(2), top + Scale(2)},
             {left + size - Scale(4), top + Scale(4)}},
            {{left + Scale(4), top + size - Scale(4)},
             {left + Scale(2), top + size - Scale(2)}}
        };
        for (const auto& ray : rays) {
            MoveToEx(dc, ray[0].x, ray[0].y, nullptr);
            LineTo(dc, ray[1].x, ray[1].y);
        }
    } else {
        Arc(dc, left + Scale(3), top + Scale(1), left + size - Scale(2),
            top + size - Scale(1), left + size - Scale(2), top + Scale(3),
            left + Scale(6), top + size - Scale(3));
        Arc(dc, left + Scale(5), top + Scale(1), left + size + Scale(2),
            top + size - Scale(1), left + Scale(6), top + size - Scale(3),
            left + size - Scale(2), top + Scale(3));
    }
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void DrawSvgBitmap(HDC dc, HBITMAP icon, const RECT& destination) {
    if (!icon) return;
    BITMAP details{};
    GetObjectW(icon, sizeof(details), &details);
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(source, icon);
    int left = destination.left + (RectWidth(destination) - details.bmWidth) / 2;
    int top = destination.top + (RectHeight(destination) - details.bmHeight) / 2;
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    AlphaBlend(dc, left, top, details.bmWidth, details.bmHeight,
               source, 0, 0, details.bmWidth, details.bmHeight, blend);
    SelectObject(source, old);
    DeleteDC(source);
}

void DrawThemeIcon(HDC dc) {
    HBITMAP icon = g.darkMode ? g.sunIcon : g.moonIcon;
    if (icon) DrawSvgBitmap(dc, icon, g.themeButtonRect);
    else DrawFallbackThemeIcon(dc, g.themeButtonRect, g.darkMode);
}

void DrawCompositedTitle(HDC target, const RECT& destination) {
    int width = RectWidth(destination);
    int height = RectHeight(destination);
    if (width <= 0 || height <= 0) return;

    HDC paint = CreateCompatibleDC(target);
    if (!paint) return;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(target, &info, DIB_RGB_COLORS,
                                      &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        DeleteDC(paint);
        return;
    }
    ZeroMemory(bits, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    HGDIOBJ oldBitmap = SelectObject(paint, bitmap);
    HGDIOBJ oldFont = SelectObject(paint, g.bodyFont);

    HTHEME theme = OpenThemeData(nullptr, L"CompositedWindow::Window");
    if (!theme) theme = OpenThemeData(nullptr, L"Window");
    if (theme) {
        DTTOPTS options{sizeof(options)};
        options.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
        options.crText = P().titleText;
        RECT local{0, 0, width, height};
        DrawThemeTextEx(theme, paint, 0, 0, L"WindowCue", -1,
                        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX,
                        &local, &options);
        CloseThemeData(theme);
        BitBlt(target, destination.left, destination.top, width, height,
               paint, 0, 0, SRCCOPY);
    }

    SelectObject(paint, oldFont);
    SelectObject(paint, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(paint);
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

// BeginPaint preserves this nonrectangular update region. Keep live DWM
// surfaces out of decoration-only GDI paints, including focus and hover changes.
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

    // Use DWM's actual caption-button bounds whenever they are available so the
    // custom theme button keeps native spacing at every DPI scale.
    RECT captionBounds{};
    if (SUCCEEDED(DwmGetWindowAttribute(
            g.hwnd, kDwmWindowAttributeCaptionButtonBounds,
            &captionBounds, sizeof(captionBounds))) &&
        RectWidth(captionBounds) >= Scale(90) && RectHeight(captionBounds) > 0) {
        int nativeButtonWidth = RectWidth(captionBounds) / 3;
        g.minButtonRect = MakeRect(captionBounds.left, captionBounds.top,
                                   captionBounds.left + nativeButtonWidth,
                                   captionBounds.bottom);
        g.maxButtonRect = MakeRect(g.minButtonRect.right, captionBounds.top,
                                   captionBounds.right - nativeButtonWidth,
                                   captionBounds.bottom);
        g.closeButtonRect = MakeRect(g.maxButtonRect.right, captionBounds.top,
                                     captionBounds.right, captionBounds.bottom);
    }
    g.themeButtonRect = MakeRect(g.minButtonRect.left - Scale(38), 0,
                                 g.minButtonRect.left, titleHeight);
    g.settingsButtonRect = MakeRect(g.themeButtonRect.left - Scale(38), 0,
                                    g.themeButtonRect.left, titleHeight);
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

void DrawInterface(HDC target, const RECT& updateRect) {
    RECT client{};
    GetClientRect(g.hwnd, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    HBRUSH background = CreateSolidBrush(P().windowBackground);
    FillRect(dc, &client, background);
    DeleteObject(background);

    // The top strip is extended DWM glass. Clearing its RGB surface lets DWM
    // provide one authoritative caption color instead of additively blending a
    // second client color into the left/right islands.
    HBRUSH transparentCaption = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &g.titleRect, transparentCaption);
    DeleteObject(transparentCaption);

    int iconSize = Scale(18);
    if (g.appIcon) DrawIconEx(dc, Scale(10), (Scale(34) - iconSize) / 2,
                              g.appIcon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    RECT appTitle = MakeRect(Scale(36), 0, g.settingsButtonRect.left - Scale(4), Scale(34));

    if (g.settingsButtonHovered || g.settingsButtonPressed ||
        (g.settingsWindow && IsWindowVisible(g.settingsWindow))) {
        RECT hoverRect = g.settingsButtonRect;
        InflateRect(&hoverRect, -Scale(3), -Scale(3));
        FillRounded(dc, hoverRect, Scale(5),
                    g.settingsButtonPressed ? P().themePressed : P().themeHover);
    }
    HBITMAP settingsIcon = g.darkMode ? g.settingsDarkIcon : g.settingsLightIcon;
    if (settingsIcon) DrawSvgBitmap(dc, settingsIcon, g.settingsButtonRect);
    else DrawTextValue(dc, L"\x2699", g.settingsButtonRect, g.bodyFont, P().mainText,
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (g.themeButtonHovered || g.themeButtonPressed) {
        RECT hoverRect = g.themeButtonRect;
        InflateRect(&hoverRect, -Scale(3), -Scale(3));
        FillRounded(dc, hoverRect, Scale(5),
                    g.themeButtonPressed ? P().themePressed : P().themeHover);
    }
    DrawThemeIcon(dc);

    FillRounded(dc, g.previewOuter, Scale(12), P().previewBackground);
    StrokeRounded(dc, g.previewOuter, Scale(12), Scale(1), P().previewBorder);
    if (g.windows.empty()) {
        DrawTextValue(dc, T().noWindows, g.previewInner, g.bodyFont, P().mutedText,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    int savedClip = SaveDC(dc);
    IntersectClipRect(dc, g.previewInner.left, g.previewInner.top,
                      g.previewInner.right, g.previewInner.bottom);
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
        if (displayIcon) DrawIconEx(dc, item.cardRect.left + Scale(9), item.cardRect.top + Scale(7),
                                   displayIcon, itemIcon, itemIcon, 0, nullptr, DI_NORMAL);
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
            DrawIconEx(dc, selfIconLeft, selfIconTop, displayIcon,
                       selfIconSize, selfIconSize, 0, nullptr, DI_NORMAL);
        }
    }
    RestoreDC(dc, savedClip);

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
        if (selectedWindowIcon) DrawIconEx(
            dc, g.bindingRect.left + Scale(16),
            g.bindingRect.top + (RectHeight(g.bindingRect) - selectedIcon) / 2,
            selectedWindowIcon, selectedIcon, selectedIcon, 0, nullptr, DI_NORMAL);
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
                  g.bodyFont, hasSelection ? P().buttonText : P().disabledText,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (!IsZoomed(g.hwnd)) {
        HBRUSH frame = CreateSolidBrush(
            g.windowActive ? P().activeWindowBorder : P().inactiveWindowBorder);
        FrameRect(dc, &client, frame);
        DeleteObject(frame);
    }

    RECT paintRect{};
    if (IntersectRect(&paintRect, &client, &updateRect)) {
        BitBlt(target, paintRect.left, paintRect.top,
               RectWidth(paintRect), RectHeight(paintRect), dc,
               paintRect.left, paintRect.top, SRCCOPY);
        RECT titlePaint{};
        if (IntersectRect(&titlePaint, &paintRect, &appTitle)) {
            DrawCompositedTitle(target, appTitle);
        }
    }
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
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
    BOOL useDarkMode = g.darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kDwmUseImmersiveDarkMode,
                          &useDarkMode, sizeof(useDarkMode));
    // The extended title strip is cleared in DrawInterface, so DWM owns the
    // final caption color across the icon, title and system-button regions.
    COLORREF captionColor = P().windowBackground;
    COLORREF captionTextColor = P().mainText;
    DwmSetWindowAttribute(hwnd, kDwmWindowAttributeCaptionColor,
                          &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, kDwmWindowAttributeTextColor,
                          &captionTextColor, sizeof(captionTextColor));
    COLORREF systemBorder = g.windowActive ? P().activeWindowBorder
                                           : P().inactiveWindowBorder;
    DwmSetWindowAttribute(hwnd, kDwmWindowAttributeBorderColor,
                          &systemBorder, sizeof(systemBorder));
    MARGINS margins{0, 0, Scale(34), 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
}

void ApplyDwmBorder(HWND hwnd) {
    COLORREF systemBorder = g.windowActive ? P().activeWindowBorder : P().inactiveWindowBorder;
    DwmSetWindowAttribute(hwnd, kDwmWindowAttributeBorderColor,
                          &systemBorder, sizeof(systemBorder));
}

int SettingsScale(int value) {
    return MulDiv(value, static_cast<int>(g.settingsDpi), 96);
}

const wchar_t* StartupStateText(StartupState state) {
    switch (state) {
    case StartupState::Enabled: return T().startupOn;
    case StartupState::Disabled: return T().startupOff;
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
    std::wstring status = StartupStateText(g.startupStatus.state);
    if (!g.startupStatus.detail.empty()) status += L"\r\n" + g.startupStatus.detail;
    SetWindowTextW(g.startupStatusLabel, status.c_str());
    InvalidateRect(g.startupButton, nullptr, FALSE);
}

void RefreshStartupControls() {
    g.startupStatus = QueryStartupStatus();
    UpdateStartupControls();
}

void RefreshSettingsResources() {
    if (g.settingsFont) DeleteObject(g.settingsFont);
    if (g.settingsBackgroundBrush) DeleteObject(g.settingsBackgroundBrush);
    if (g.githubDarkIcon) DeleteObject(g.githubDarkIcon);
    if (g.githubLightIcon) DeleteObject(g.githubLightIcon);
    g.settingsFont = CreateFontW(-MulDiv(9, static_cast<int>(g.settingsDpi), 72),
        0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY,
        VARIABLE_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    g.settingsBackgroundBrush = CreateSolidBrush(P().windowBackground);
    g.githubDarkIcon = RenderSvgResource(g.instance, IDR_GITHUB_DARK_SVG, SettingsScale(22));
    g.githubLightIcon = RenderSvgResource(g.instance, IDR_GITHUB_LIGHT_SVG, SettingsScale(22));
    for (HWND control : {g.startupButton, g.githubButton, g.startupLabel,
                          g.startupStatusLabel, g.startupHintLabel}) {
        if (control) SendMessageW(control, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g.settingsFont), TRUE);
    }
}

void ApplySettingsTheme() {
    if (!g.settingsWindow) return;
    BOOL dark = g.darkMode ? TRUE : FALSE;
    COLORREF background = P().windowBackground;
    COLORREF text = P().mainText;
    DwmSetWindowAttribute(g.settingsWindow, kDwmUseImmersiveDarkMode, &dark, sizeof(dark));
    DwmSetWindowAttribute(g.settingsWindow, kDwmWindowAttributeCaptionColor,
                          &background, sizeof(background));
    DwmSetWindowAttribute(g.settingsWindow, kDwmWindowAttributeTextColor, &text, sizeof(text));
    if (g.settingsBackgroundBrush) DeleteObject(g.settingsBackgroundBrush);
    g.settingsBackgroundBrush = CreateSolidBrush(background);
    RedrawWindow(g.settingsWindow, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void LayoutSettingsControls() {
    if (!g.settingsWindow) return;
    RECT client{};
    GetClientRect(g.settingsWindow, &client);
    int margin = SettingsScale(20);
    int width = RectWidth(client) - margin * 2;
    MoveWindow(g.startupLabel, margin, SettingsScale(22), width - SettingsScale(112),
               SettingsScale(28), TRUE);
    MoveWindow(g.startupButton, client.right - margin - SettingsScale(96), SettingsScale(16),
               SettingsScale(96), SettingsScale(36), TRUE);
    MoveWindow(g.startupStatusLabel, margin, SettingsScale(64), width, SettingsScale(70), TRUE);
    MoveWindow(g.startupHintLabel, margin, SettingsScale(142), width, SettingsScale(32), TRUE);
    MoveWindow(g.githubButton, margin, SettingsScale(184), width, SettingsScale(40), TRUE);
}

void HideSettingsWindow() {
    if (!g.settingsWindow) return;
    ShowWindow(g.settingsWindow, SW_HIDE);
    InvalidateRect(g.hwnd, &g.settingsButtonRect, FALSE);
}

void OpenSettingsLink(const wchar_t* link) {
    HINSTANCE result = ShellExecuteW(g.settingsWindow, L"open", link,
                                     nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        SetWindowTextW(g.startupStatusLabel, T().linkError);
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
    FillRect(item.hDC, &rect, g.settingsBackgroundBrush);
    if (item.CtlID == kStartupButtonId) {
        bool on = g.startupStatus.state == StartupState::Enabled;
        COLORREF fill = on ? RGB(15, 122, 96) : P().disabledButton;
        FillRounded(item.hDC, rect, SettingsScale(32), fill);
        StrokeRounded(item.hDC, rect, SettingsScale(32), SettingsScale(1),
                       enabled ? P().hoverBorder : P().disabledButtonBorder);
        int knob = SettingsScale(24);
        int left = on ? rect.right - SettingsScale(6) - knob : rect.left + SettingsScale(6);
        int top = rect.top + (RectHeight(rect) - knob) / 2;
        RECT knobRect = MakeRect(left, top, left + knob, top + knob);
        FillRounded(item.hDC, knobRect, knob, on ? RGB(245, 248, 252) : P().mutedText);
        RECT label = rect;
        if (on) label.right = knobRect.left;
        else label.left = knobRect.right;
        DrawTextValue(item.hDC, on ? T().startupOn : T().startupOff, label,
                      g.settingsFont, on ? RGB(245, 248, 252) :
                      (enabled ? P().mainText : P().disabledText),
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        FillRounded(item.hDC, rect, SettingsScale(8), pressed ? P().themePressed : P().panelBackground);
        StrokeRounded(item.hDC, rect, SettingsScale(8), SettingsScale(1), P().panelBorder);
        RECT iconRect = MakeRect(rect.left + SettingsScale(12), rect.top,
                                 rect.left + SettingsScale(40), rect.bottom);
        HBITMAP icon = g.darkMode ? g.githubDarkIcon : g.githubLightIcon;
        DrawSvgBitmap(item.hDC, icon, iconRect);
        RECT label = rect;
        label.left += SettingsScale(48);
        DrawTextValue(item.hDC, T().github, label, g.settingsFont, P().mainText,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    if (item.itemState & ODS_FOCUS) {
        RECT focus = rect;
        InflateRect(&focus, -SettingsScale(4), -SettingsScale(4));
        DrawFocusRect(item.hDC, &focus);
    }
}

LRESULT CALLBACK SettingsProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g.settingsWindow = hwnd;
        g.settingsDpi = QueryWindowDpi(hwnd);
        g.startupLabel = CreateWindowW(L"STATIC", T().startup, WS_CHILD | WS_VISIBLE,
            0, 0, 0, 0, hwnd, nullptr, g.instance, nullptr);
        g.startupStatusLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
            0, 0, 0, 0, hwnd, nullptr, g.instance, nullptr);
        g.startupHintLabel = CreateWindowW(L"STATIC", T().startupHint,
            WS_CHILD | WS_VISIBLE | SS_NOPREFIX, 0, 0, 0, 0, hwnd, nullptr, g.instance, nullptr);
        g.startupButton = CreateWindowW(L"BUTTON", T().startup,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartupButtonId)),
            g.instance, nullptr);
        g.githubButton = CreateWindowW(L"BUTTON", T().github,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGitHubButtonId)),
            g.instance, nullptr);
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
    case WM_DRAWITEM:
        DrawSettingsButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, reinterpret_cast<HWND>(lParam) == g.startupLabel ?
                     P().mainText : P().mutedText);
        return reinterpret_cast<LRESULT>(g.settingsBackgroundBrush);
    }
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect, g.settingsBackgroundBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        FillRect(dc, &paint.rcPaint, g.settingsBackgroundBrush);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_CLOSE:
        HideSettingsWindow();
        SetFocus(g.hwnd);
        return 0;
    case WM_DESTROY:
        if (g.settingsFont) DeleteObject(g.settingsFont);
        if (g.settingsBackgroundBrush) DeleteObject(g.settingsBackgroundBrush);
        if (g.githubDarkIcon) DeleteObject(g.githubDarkIcon);
        if (g.githubLightIcon) DeleteObject(g.githubLightIcon);
        g.settingsFont = nullptr;
        g.settingsBackgroundBrush = nullptr;
        g.githubDarkIcon = g.githubLightIcon = nullptr;
        g.settingsWindow = g.startupButton = g.githubButton = nullptr;
        g.startupLabel = g.startupStatusLabel = g.startupHintLabel = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void ShowSettingsWindow() {
    EndCapture(true);
    if (!g.settingsWindow) {
        WNDCLASSW wc{};
        wc.hInstance = g.instance;
        wc.lpfnWndProc = SettingsProcedure;
        wc.lpszClassName = kSettingsClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        RECT size = MakeRect(0, 0, Scale(410), Scale(240));
        constexpr DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
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
    g.darkMode = !g.darkMode;
    SaveDarkModePreference(g.darkMode);
    ApplyDwmFrame(g.hwnd);
    ApplySettingsTheme();
    SetWindowPos(g.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                 SWP_NOZORDER | SWP_NOACTIVATE);
    RedrawWindow(g.hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

LRESULT HitTest(POINT screenPoint) {
    POINT client = screenPoint;
    ScreenToClient(g.hwnd, &client);
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
    LRESULT dwmResult = 0;
    BOOL dwmHandled = DwmDefWindowProc(hwnd, message, wParam, lParam, &dwmResult);
    if (message == WM_NCHITTEST && dwmHandled) return dwmResult;

    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;
    case WM_NCHITTEST: {
        if (dwmHandled) return dwmResult;
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        return HitTest(point);
    }
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = Scale(760);
        info->ptMinTrackSize.y = Scale(310);
        return 0;
    }
    case WM_CREATE:
        g.hwnd = hwnd;
        g.dpi = QueryWindowDpi(hwnd);
        CreateFonts();
        RefreshThemeIcons();
        ApplyDwmFrame(hwnd);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_THEMECHANGED:
        ApplyDwmFrame(hwnd);
        InvalidateRect(hwnd, &g.titleRect, FALSE);
        return 0;
    case WM_SIZE:
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
        if (wParam == SPI_SETWORKAREA) {
            ResizeForContent();
            Layout();
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateThumbnails();
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        Layout();
        DrawInterface(dc, paint.rcPaint);
        EndPaint(hwnd, &paint);
        UpdateThumbnails();
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
        bool themeHovered = PointInside(g.themeButtonRect, point);
        if (themeHovered != g.themeButtonHovered) {
            g.themeButtonHovered = themeHovered;
            InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
        }
        bool settingsHovered = PointInside(g.settingsButtonRect, point);
        if (settingsHovered != g.settingsButtonHovered) {
            g.settingsButtonHovered = settingsHovered;
            InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
        }
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
        if (g.themeButtonHovered) InvalidateRect(hwnd, &g.themeButtonRect, FALSE);
        g.themeButtonHovered = false;
        if (g.settingsButtonHovered) InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
        g.settingsButtonHovered = false;
        return 0;
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (PointInside(g.settingsButtonRect, point)) {
            g.settingsButtonPressed = true;
            g.settingsButtonHovered = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
            return 0;
        }
        if (PointInside(g.themeButtonRect, point)) {
            g.themeButtonPressed = true;
            g.themeButtonHovered = true;
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
        if (g.settingsButtonPressed) {
            bool open = PointInside(g.settingsButtonRect, point);
            g.settingsButtonPressed = false;
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
        g.draggingScrollbar = false;
        if (g.settingsButtonPressed) InvalidateRect(hwnd, &g.settingsButtonRect, FALSE);
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
        if (!g.windowActive) EndCapture(true);
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
        if (wParam == kTrayRetryTimer) {
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
        KillTimer(hwnd, kThumbnailTimer);
        KillTimer(hwnd, kRefreshTimer);
        KillTimer(hwnd, kTrayRetryTimer);
        if (g.settingsWindow) DestroyWindow(g.settingsWindow);
        UnregisterAllBindings();
        Shell_NotifyIconW(NIM_DELETE, &g.tray);
        ReleaseWindows(g.windows);
        DeleteFonts();
        DeleteThemeIcons();
        if (g.appIcon) DestroyIcon(g.appIcon);
        g.appIcon = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return dwmHandled ? dwmResult : DefWindowProcW(hwnd, message, wParam, lParam);
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
    if (CommandLineHasOption(L"--svg-self-test")) {
        const int resources[] = {IDR_SUN_SVG, IDR_MOON_SVG,
            IDR_SETTINGS_DARK_SVG, IDR_SETTINGS_LIGHT_SVG,
            IDR_GITHUB_DARK_SVG, IDR_GITHUB_LIGHT_SVG};
        bool rendered = true;
        for (int resource : resources) {
            HBITMAP bitmap = RenderSvgResource(instance, resource, 18);
            rendered = rendered && bitmap != nullptr;
            if (bitmap) DeleteObject(bitmap);
        }
        CoUninitialize();
        return rendered ? 0 : 3;
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
        instance, MAKEINTRESOURCEW(IDI_WINDOWCUE), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));

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
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
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
