#pragma once

#include <windows.h>
#include <dwmapi.h>
#include <algorithm>

namespace windowcue {

struct ChromeStatus {
    HRESULT extension{S_OK};
    HRESULT decoration{S_OK};
    HRESULT corners{E_NOTIMPL};
    HRESULT border{E_NOTIMPL};
    bool nativeRounded() const { return SUCCEEDED(corners); }
    bool nativeBorder() const { return SUCCEEDED(border); }
};

inline ChromeStatus ConfigureOpaqueChrome(HWND hwnd, COLORREF borderColor, bool darkMode,
                                          bool allowRounding = true) {
    MARGINS margins{};
    // Disabling DWM does not disable User32's classic nonclient painter. Keep
    // composition enabled; NCCALCSIZE gives this window one full client area.
    DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    ChromeStatus result{
        DwmExtendFrameIntoClientArea(hwnd, &margins),
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy)),
        E_NOTIMPL,
        E_NOTIMPL
    };

    // Match DWM dark shadow/menu framing on Windows 10 2004+ and Windows 11.
    constexpr auto kImmersiveDark = static_cast<DWMWINDOWATTRIBUTE>(20);
    BOOL darkValue = darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kImmersiveDark, &darkValue, sizeof(darkValue));

    // Windows 11 native hardware-composited rounded corners (DWMWCP_ROUND = 2)
    // and theme-matched 1px outer frame border (DWMWA_BORDER_COLOR = 34).
    // Older Windows 10 builds cleanly reject these optional attributes and fall
    // back to client-drawn framing without affecting geometry.
    constexpr auto kCornerPreference = static_cast<DWMWINDOWATTRIBUTE>(33);
    constexpr auto kBorderColor = static_cast<DWMWINDOWATTRIBUTE>(34);
    const DWORD corner = (allowRounding && !IsZoomed(hwnd)) ? 2u /* DWMWCP_ROUND */
                                                            : 1u /* DWMWCP_DONOTROUND */;
    result.corners = DwmSetWindowAttribute(hwnd, kCornerPreference, &corner, sizeof(corner));
    result.border = DwmSetWindowAttribute(hwnd, kBorderColor, &borderColor, sizeof(borderColor));

    if (FAILED(result.extension) || FAILED(result.decoration)) {
        OutputDebugStringW(L"WindowCue: optional DWM decoration unavailable; client chrome remains opaque.\n");
    }
    return result;
}

inline HRESULT UpdateChromeBorderColor(HWND hwnd, COLORREF borderColor, bool allowRounding = true) {
    constexpr auto kCornerPreference = static_cast<DWMWINDOWATTRIBUTE>(33);
    constexpr auto kBorderColor = static_cast<DWMWINDOWATTRIBUTE>(34);
    const DWORD corner = (allowRounding && !IsZoomed(hwnd)) ? 2u : 1u;
    DwmSetWindowAttribute(hwnd, kCornerPreference, &corner, sizeof(corner));
    return DwmSetWindowAttribute(hwnd, kBorderColor, &borderColor, sizeof(borderColor));
}

// Cloak before initial show so DWM never exposes an uninitialized black frame
// while Direct2D creates or presents the window's first swap-chain buffer.
inline void SetWindowCloaked(HWND hwnd, bool cloaked) {
    if (!hwnd) return;
    constexpr auto kCloak = static_cast<DWMWINDOWATTRIBUTE>(13); // DWMWA_CLOAK
    BOOL value = cloaked ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, kCloak, &value, sizeof(value));
}

inline void RevealWindowWithoutFlash(HWND hwnd, int left, int top, bool move, bool activate) {
    if (!hwnd) return;
    const bool wasVisible = IsWindowVisible(hwnd) != FALSE;
    if (!wasVisible) SetWindowCloaked(hwnd, true);
    UINT flags = SWP_NOSIZE | SWP_SHOWWINDOW | (move ? 0u : SWP_NOMOVE) |
                 (activate ? 0u : SWP_NOACTIVATE);
    SetWindowPos(hwnd, HWND_TOP, left, top, 0, 0, flags);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
    if (!wasVisible) {
        // Two flushes let DWM latch a newly created DXGI/D2D swap-chain frame
        // before uncloaking, guaranteeing frame 0 is never black.
        DwmFlush();
        DwmFlush();
        SetWindowCloaked(hwnd, false);
    }
}

inline LRESULT ResizeBorderHit(HWND hwnd, UINT dpi, POINT p) {
    if (IsZoomed(hwnd) || !(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME)) return HTNOWHERE;
    using MetricForDpi = int(WINAPI*)(int, UINT);
    static auto metric = reinterpret_cast<MetricForDpi>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    auto get = [&](int id) { return metric ? metric(id, dpi) : GetSystemMetrics(id); };
    int x = std::max(1, get(SM_CXSIZEFRAME) + get(SM_CXPADDEDBORDER));
    int y = std::max(1, get(SM_CYSIZEFRAME) + get(SM_CXPADDEDBORDER));
    RECT r{};
    GetClientRect(hwnd, &r);
    bool l = p.x < x, right = p.x >= r.right - x, top = p.y < y, bottom = p.y >= r.bottom - y;
    if (top && l) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && l) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    return l ? HTLEFT : right ? HTRIGHT : top ? HTTOP : bottom ? HTBOTTOM : HTNOWHERE;
}

inline void SetMaximizedWorkArea(HWND hwnd, MINMAXINFO& info) {
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
        info.ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                              monitor.rcWork.top - monitor.rcMonitor.top};
        info.ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                          monitor.rcWork.bottom - monitor.rcWork.top};
    }
}

} // namespace windowcue
