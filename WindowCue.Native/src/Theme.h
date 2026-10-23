#pragma once
#include <cmath>

namespace windowcue {
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
    COLORREF unbindText;
};

inline ThemePalette MakeDarkPalette() {
    ThemePalette p{};
    p.windowBackground = RGB(32, 35, 41);
    p.panelBackground = RGB(38, 41, 48);
    p.cardBackground = RGB(32, 35, 41);
    p.normalBorder = RGB(67, 73, 83);
    p.hoverBorder = RGB(94, 94, 94);
    p.selectedBorder = RGB(16, 163, 127);
    p.boundBorder = RGB(126, 211, 162);
    p.mutedText = RGB(180, 180, 180);
    p.mainText = RGB(236, 236, 236);
    p.inputBackground = RGB(24, 24, 24);
    p.errorBorder = RGB(239, 107, 107);
    p.activeWindowBorder = RGB(82, 84, 90);
    p.inactiveWindowBorder = RGB(58, 60, 66);
    p.titleText = RGB(236, 236, 236);
    p.previewBackground = RGB(38, 41, 48);
    p.previewBorder = RGB(67, 73, 83);
    p.previewCanvas = RGB(32, 35, 41);
    p.processText = RGB(166, 166, 166);
    p.scrollTrack = RGB(43, 43, 43);
    p.scrollThumb = RGB(104, 104, 104);
    p.panelBorder = RGB(67, 73, 83);
    p.inputBorder = RGB(86, 86, 86);
    p.placeholderText = RGB(155, 155, 155);
    p.disabledText = RGB(160, 160, 160);
    p.disabledButton = RGB(52, 52, 52);
    p.disabledButtonBorder = RGB(75, 75, 75);
    p.unbindButton = RGB(179, 38, 30);
    p.unbindButtonBorder = RGB(201, 72, 64);
    p.bindButton = RGB(236, 236, 236);
    p.bindButtonBorder = RGB(236, 236, 236);
    p.themeHover = RGB(52, 52, 52);
    p.themePressed = RGB(65, 65, 65);
    p.buttonText = RGB(23, 23, 23);
    p.unbindText = RGB(245, 248, 252);
    return p;
}

inline ThemePalette MakeLightPalette() {
    ThemePalette p{};
    p.windowBackground = RGB(245, 248, 252);
    p.panelBackground = RGB(238, 243, 249);
    p.cardBackground = RGB(245, 248, 252);
    p.normalBorder = RGB(211, 213, 216);
    p.hoverBorder = RGB(157, 160, 164);
    p.selectedBorder = RGB(15, 143, 112);
    p.boundBorder = RGB(22, 135, 94);
    p.mutedText = RGB(91, 95, 101);
    p.mainText = RGB(35, 36, 38);
    p.inputBackground = RGB(245, 248, 252);
    p.errorBorder = RGB(201, 63, 63);
    p.activeWindowBorder = RGB(188, 190, 194);
    p.inactiveWindowBorder = RGB(211, 213, 216);
    p.titleText = RGB(47, 48, 51);
    p.previewBackground = RGB(238, 243, 249);
    p.previewBorder = RGB(223, 224, 226);
    p.previewCanvas = RGB(233, 238, 245);
    p.processText = RGB(101, 105, 111);
    p.scrollTrack = RGB(232, 233, 235);
    p.scrollThumb = RGB(168, 170, 174);
    p.panelBorder = RGB(218, 219, 222);
    p.inputBorder = RGB(176, 178, 182);
    p.placeholderText = RGB(105, 109, 115);
    p.disabledText = RGB(96, 98, 102);
    p.disabledButton = RGB(230, 231, 233);
    p.disabledButtonBorder = RGB(207, 209, 212);
    p.unbindButton = RGB(179, 38, 30);
    p.unbindButtonBorder = RGB(140, 29, 24);
    p.bindButton = RGB(35, 36, 38);
    p.bindButtonBorder = RGB(35, 36, 38);
    p.themeHover = RGB(226, 227, 229);
    p.themePressed = RGB(214, 215, 218);
    p.buttonText = RGB(245, 248, 252);
    p.unbindText = RGB(245, 248, 252);
    return p;
}

inline bool& HighContrastEnabled() { static bool active{}; return active; }
inline ThemePalette& HighContrastPalette() { static ThemePalette p{}; return p; }
inline void RefreshSystemTheme() {
    HIGHCONTRASTW hc{sizeof(hc)};
    HighContrastEnabled() = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
                             (hc.dwFlags & HCF_HIGHCONTRASTON);
    auto& p=HighContrastPalette();
    p=MakeDarkPalette();
    COLORREF bg=GetSysColor(COLOR_WINDOW), text=GetSysColor(COLOR_WINDOWTEXT);
    p.windowBackground=p.panelBackground=p.cardBackground=p.previewBackground=p.previewCanvas=
        p.inputBackground=p.scrollTrack=bg;
    p.mainText=p.titleText=p.processText=p.mutedText=p.placeholderText=text;
    p.normalBorder=p.panelBorder=p.previewBorder=p.inputBorder=p.activeWindowBorder=
        p.inactiveWindowBorder=GetSysColor(COLOR_WINDOWTEXT);
    p.hoverBorder=p.selectedBorder=p.boundBorder=p.scrollThumb=p.errorBorder=GetSysColor(COLOR_HIGHLIGHT);
    p.bindButton=p.unbindButton=p.themeHover=p.themePressed=GetSysColor(COLOR_HIGHLIGHT);
    p.buttonText=p.unbindText=GetSysColor(COLOR_HIGHLIGHTTEXT);
    p.bindButtonBorder=p.unbindButtonBorder=GetSysColor(COLOR_HIGHLIGHT);
    p.disabledButton=GetSysColor(COLOR_BTNFACE);
    p.disabledText=p.disabledButtonBorder=GetSysColor(COLOR_GRAYTEXT);
}
inline const ThemePalette& GetThemePalette(bool dark) {
    static const ThemePalette night=MakeDarkPalette(), day=MakeLightPalette();
    return HighContrastEnabled() ? HighContrastPalette() : dark ? night : day;
}
inline double ColorContrast(COLORREF a,COLORREF b) {
    auto luminance=[](COLORREF c) {
        auto linear=[](BYTE value) {double v=value/255.;return v<=.04045 ? v/12.92 : std::pow((v+.055)/1.055,2.4);};
        return .2126*linear(GetRValue(c))+.7152*linear(GetGValue(c))+.0722*linear(GetBValue(c));
    };
    double x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);
}
inline bool ValidateThemePalettes() {
    for(const auto& p : {MakeDarkPalette(),MakeLightPalette()})
        if(ColorContrast(p.buttonText,p.bindButton)<4.5 || ColorContrast(p.unbindText,p.unbindButton)<4.5 ||
           ColorContrast(p.titleText,p.windowBackground)<4.5 || ColorContrast(p.mainText,p.cardBackground)<4.5 ||
           ColorContrast(p.mutedText,p.panelBackground)<4.5) return false;
    return true;
}
} // namespace windowcue
