#include "../src/WindowCueNative.h"
#include "../src/RenderContext.h"
#include <cstdio>

namespace {
windowcue::RenderContext renderer;
bool dark{}, software{};
COLORREF sourceColor = RGB(37,151,204);
LRESULT CALLBACK Source(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT r{};GetClientRect(hwnd,&r);
        HBRUSH brush=CreateSolidBrush(sourceColor);FillRect(dc,&r,brush);DeleteObject(brush);
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT CALLBACK Host(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        auto region=windowcue::WindowPaintRegion(hwnd);
        PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);
        if (renderer.Begin(hwnd,96,ps.rcPaint,software)) {
            bool clipped=renderer.PushRegion(region);
            RECT r{};GetClientRect(hwnd,&r);
            renderer.Fill(r,dark?RGB(32,35,41):RGB(245,248,252));
            renderer.Rounded({16,48,216,208},8,dark?RGB(40,43,49):RGB(238,243,249));
            renderer.Rounded({232,48,432,208},8,dark?RGB(40,43,49):RGB(238,243,249));
            renderer.Icon(windowcue::VectorIcon::Settings,{360,0,400,34},dark?RGB(236,236,236):RGB(35,36,38));
            if(clipped) renderer.PopRegion();
            renderer.End();
        }
        EndPaint(hwnd,&ps);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void Pump(int ms) {
    ULONGLONG until=GetTickCount64()+ms;
    do { MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
        TranslateMessage(&msg);DispatchMessageW(&msg); }
        MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);
    } while(GetTickCount64()<until);
}
}
int wmain() {
    // Only purpose-built windows are rendered or read; no user settings, startup
    // registration, global shortcut, app enumeration or screenshot is used.
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND oldForeground=GetForegroundWindow();
    HINSTANCE app=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=app;wc.lpfnWndProc=Host;wc.lpszClassName=L"WindowCue.VectorProbe";
    RegisterClassW(&wc);wc.lpfnWndProc=Source;wc.lpszClassName=L"WindowCue.VectorSource";RegisterClassW(&wc);
    HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"WindowCue.VectorProbe",L"Vector probe",WS_POPUP,20,20,460,230,nullptr,nullptr,app,nullptr);
    HWND source=CreateWindowExW(WS_EX_TOOLWINDOW,L"WindowCue.VectorSource",L"Vector source",WS_POPUP,500,400,240,160,nullptr,nullptr,app,nullptr);
    if(!host || !source) return 90;
    ShowWindow(source,SW_SHOWNOACTIVATE);ShowWindow(host,SW_SHOWNOACTIVATE);
    HTHUMBNAIL thumbnail{};HRESULT hr=DwmRegisterThumbnail(host,source,&thumbnail);
    if(FAILED(hr)) return 91;
    DWM_THUMBNAIL_PROPERTIES tp{};tp.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_VISIBLE|DWM_TNP_OPACITY;
    tp.rcDestination={24,76,204,196};tp.fVisible=TRUE;tp.opacity=255;
    int failed=0;
    for(bool sw:{false,true}) {
        software=sw;
        for(int i=0;i<12;++i) {
            dark=(i%2)!=0;
            sourceColor=i%3==0?RGB(37,151,204):i%3==1?RGB(201,95,43):RGB(65,170,101);
            InvalidateRect(source,nullptr,FALSE);UpdateWindow(source);
            if(i==6) renderer.Discard(); // Simulate device-dependent resource loss.
            InvalidateRect(host,nullptr,FALSE);UpdateWindow(host);
            // A static geometry must stay composited without re-submission
            // on every theme frame, including render-target recreation.
            if(i==0 && FAILED(DwmUpdateThumbnailProperties(thumbnail,&tp))) failed=92;
            Pump(120);DwmFlush();
            if(FAILED(renderer.LastError())) failed=97;
            POINT p{100,130};ClientToScreen(host,&p);
            if(WindowFromPoint(p)!=host) { failed=93;break; }
            HDC screen=GetDC(nullptr);COLORREF value=GetPixel(screen,p.x,p.y);ReleaseDC(nullptr,screen);
            if(value!=sourceColor) { std::printf("preview mismatch sample=%d backend=%d actual=%06lx expected=%06lx\n",i,sw,value,sourceColor); failed=94; }
            p={250,20};ClientToScreen(host,&p);screen=GetDC(nullptr);value=GetPixel(screen,p.x,p.y);ReleaseDC(nullptr,screen);
            COLORREF expected=dark?RGB(32,35,41):RGB(245,248,252);
            if(value!=expected) failed=95;
        }
        POINT hole{340,120};ClientToScreen(host,&hole);
        HDC screen=GetDC(nullptr);COLORREF before=GetPixel(screen,hole.x,hole.y);ReleaseDC(nullptr,screen);
        HRGN update=CreateRectRgn(0,0,460,230), exclude=CreateRectRgn(250,80,420,190);
        CombineRgn(update,update,exclude,RGN_DIFF);dark=!dark;
        InvalidateRgn(host,update,FALSE);DeleteObject(update);DeleteObject(exclude);
        UpdateWindow(host);Pump(80);DwmFlush();
        screen=GetDC(nullptr);COLORREF after=GetPixel(screen,hole.x,hole.y);ReleaseDC(nullptr,screen);
        POINT outside{300,20};ClientToScreen(host,&outside);
        screen=GetDC(nullptr);COLORREF changed=GetPixel(screen,outside.x,outside.y);ReleaseDC(nullptr,screen);
        if(before!=after || changed!=(dark?RGB(32,35,41):RGB(245,248,252)) || FAILED(renderer.LastError())) failed=96;
        std::printf("VECTOR_PREVIEW backend=%s result=%d generations=%u complex_clip_preserved=%d\n",sw?"software":"default",failed,renderer.Generation(),before==after);
    }
    DwmUnregisterThumbnail(thumbnail);renderer.Discard();DestroyWindow(host);DestroyWindow(source);
    if(oldForeground && IsWindow(oldForeground)) SetForegroundWindow(oldForeground);
    CoUninitialize();return failed;
}
