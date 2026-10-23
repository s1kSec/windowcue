#pragma once

namespace windowcue {
struct ChromeStatus { HRESULT extension{S_OK}, decoration{S_OK}; };
inline ChromeStatus ConfigureOpaqueChrome(HWND hwnd) {
    MARGINS margins{};
    DWMNCRENDERINGPOLICY policy=DWMNCRP_DISABLED;
    ChromeStatus result{DwmExtendFrameIntoClientArea(hwnd,&margins),
        DwmSetWindowAttribute(hwnd,DWMWA_NCRENDERING_POLICY,&policy,sizeof(policy))};
    if(FAILED(result.extension) || FAILED(result.decoration))
        OutputDebugStringW(L"WindowCue: optional DWM decoration unavailable; client chrome remains opaque.\n");
    return result;
}
inline LRESULT ResizeBorderHit(HWND hwnd,UINT dpi,POINT p) {
    if(IsZoomed(hwnd) || !(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_THICKFRAME)) return HTNOWHERE;
    using MetricForDpi=int(WINAPI*)(int,UINT);
    static auto metric=reinterpret_cast<MetricForDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetSystemMetricsForDpi"));
    auto get=[&](int id) { return metric ? metric(id,dpi) : GetSystemMetrics(id); };
    int x=std::max(1,get(SM_CXSIZEFRAME)+get(SM_CXPADDEDBORDER));
    int y=std::max(1,get(SM_CYSIZEFRAME)+get(SM_CXPADDEDBORDER));
    RECT r{};GetClientRect(hwnd,&r);
    bool l=p.x<x,right=p.x>=r.right-x,top=p.y<y,bottom=p.y>=r.bottom-y;
    if(top && l) return HTTOPLEFT;if(top && right) return HTTOPRIGHT;
    if(bottom && l) return HTBOTTOMLEFT;if(bottom && right) return HTBOTTOMRIGHT;
    return l ? HTLEFT : right ? HTRIGHT : top ? HTTOP : bottom ? HTBOTTOM : HTNOWHERE;
}
inline void SetMaximizedWorkArea(HWND hwnd,MINMAXINFO& info) {
    MONITORINFO monitor{sizeof(monitor)};
    if(GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&monitor)) {
        info.ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};
        info.ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};
    }
}
} // namespace windowcue
