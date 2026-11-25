#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace windowcue {

struct ThemePalette {
    // Window & surface elevation hierarchy
    COLORREF windowBackground;
    COLORREF panelBackground;
    COLORREF cardBackground;
    COLORREF cardHoverBackground;
    COLORREF previewBackground;
    COLORREF previewCanvas;
    COLORREF settingsSurface;
    COLORREF divider;

    // Borders & outlines
    COLORREF normalBorder;
    COLORREF hoverBorder;
    COLORREF selectedBorder;
    COLORREF boundBorder;
    COLORREF errorBorder;
    COLORREF activeWindowBorder;
    COLORREF inactiveWindowBorder;
    COLORREF previewBorder;
    COLORREF previewInnerBorder;
    COLORREF panelBorder;
    COLORREF inputBorder;

    // Typography
    COLORREF titleText;
    COLORREF mainText;
    COLORREF mutedText;
    COLORREF processText;
    COLORREF placeholderText;
    COLORREF disabledText;

    // Interactive controls & semantic tints
    COLORREF inputBackground;
    COLORREF inputCapturingBackground;
    COLORREF inputErrorBackground;
    COLORREF keycapBackground;
    COLORREF keycapBorder;
    COLORREF iconWellBackground;
    COLORREF disabledButton;
    COLORREF disabledButtonBorder;
    COLORREF bindButton;
    COLORREF bindButtonHover;
    COLORREF bindButtonPressed;
    COLORREF bindButtonBorder;
    COLORREF buttonText;
    COLORREF unbindButton;
    COLORREF unbindButtonHover;
    COLORREF unbindButtonPressed;
    COLORREF unbindButtonBorder;
    COLORREF unbindText;
    COLORREF boundBadgeBackground;
    COLORREF boundBadgeBorder;
    COLORREF boundBadgeText;
    COLORREF warningBackground;
    COLORREF warningBorder;
    COLORREF warningText;
    COLORREF switchOffTrack;
    COLORREF switchOnTrack;
    COLORREF switchThumb;

    // Chrome & scrollbars
    COLORREF themeHover;
    COLORREF themePressed;
    COLORREF closeHover;
    COLORREF closePressed;
    COLORREF scrollTrack;
    COLORREF scrollThumb;
    COLORREF scrollThumbHover;
};

inline ThemePalette MakeDarkPalette() {
    ThemePalette p{};
    p.windowBackground = RGB(22, 24, 29);          // #16181D Antigravity obsidian slate
    p.panelBackground = RGB(28, 31, 38);           // #1C1F26 recessed workspace / dock
    p.cardBackground = RGB(34, 38, 48);            // #222630 elevated card surface
    p.cardHoverBackground = RGB(41, 46, 58);       // #292E3A subtle hover lift
    p.previewBackground = RGB(28, 31, 38);         // #1C1F26 workspace well
    p.previewCanvas = RGB(19, 21, 26);             // #13151A quiet preview backdrop
    p.settingsSurface = RGB(31, 35, 44);           // #1F232C floating popover surface
    p.divider = RGB(48, 54, 67);                   // #303643 subtle hairline divider

    p.normalBorder = RGB(49, 55, 68);              // #313744
    p.hoverBorder = RGB(76, 86, 106);              // #4C566A
    p.selectedBorder = RGB(79, 143, 247);          // #4F8FF7 Antigravity accent blue
    p.boundBorder = RGB(56, 193, 142);             // #38C18E refined emerald
    p.errorBorder = RGB(232, 98, 98);              // #E86262
    p.activeWindowBorder = RGB(62, 70, 86);        // #3E4656
    p.inactiveWindowBorder = RGB(42, 47, 58);      // #2A2F3A
    p.previewBorder = RGB(44, 49, 60);             // #2C313C
    p.previewInnerBorder = RGB(42, 47, 58);        // #2A2F3A
    p.panelBorder = RGB(46, 52, 64);               // #2E3440
    p.inputBorder = RGB(62, 69, 84);               // #3E4554

    p.titleText = RGB(228, 232, 240);              // #E4E8F0
    p.mainText = RGB(237, 240, 245);               // #EDF0F5
    p.mutedText = RGB(152, 162, 179);              // #98A2B3
    p.processText = RGB(145, 155, 174);            // #919BAE
    p.placeholderText = RGB(144, 154, 172);        // #909AAC
    p.disabledText = RGB(138, 146, 160);           // #8A92A0

    p.inputBackground = RGB(20, 22, 28);           // #14161C
    p.inputCapturingBackground = RGB(22, 30, 44);  // #161E2C soft blue focus tint
    p.inputErrorBackground = RGB(42, 24, 27);      // #2A181B
    p.keycapBackground = RGB(36, 41, 52);          // #242934
    p.keycapBorder = RGB(64, 72, 88);              // #404858
    p.iconWellBackground = RGB(37, 42, 53);        // #252A35
    p.disabledButton = RGB(37, 41, 50);            // #252932
    p.disabledButtonBorder = RGB(52, 58, 70);      // #343A46
    p.bindButton = RGB(232, 236, 243);             // #E8ECF3 crisp monochrome primary
    p.bindButtonHover = RGB(244, 247, 251);        // #F4F7FB
    p.bindButtonPressed = RGB(212, 218, 228);      // #D4DAE4
    p.bindButtonBorder = RGB(232, 236, 243);
    p.buttonText = RGB(21, 24, 30);                // #15181E
    p.unbindButton = RGB(185, 49, 45);             // #B9312D
    p.unbindButtonHover = RGB(204, 58, 54);        // #CC3A36
    p.unbindButtonPressed = RGB(158, 40, 37);      // #9E2825
    p.unbindButtonBorder = RGB(207, 74, 70);
    p.unbindText = RGB(248, 250, 252);             // #F8FAFC
    p.boundBadgeBackground = RGB(22, 51, 41);      // #163329
    p.boundBadgeBorder = RGB(43, 114, 88);         // #2B7258
    p.boundBadgeText = RGB(126, 230, 185);         // #7EE6B9
    p.warningBackground = RGB(48, 40, 24);         // #302818
    p.warningBorder = RGB(114, 90, 42);            // #725A2A
    p.warningText = RGB(242, 203, 128);            // #F2CB80
    p.switchOffTrack = RGB(54, 60, 74);            // #363C4A
    p.switchOnTrack = RGB(46, 158, 118);           // #2E9E76
    p.switchThumb = RGB(245, 248, 252);            // #F5F8FC

    p.themeHover = RGB(42, 47, 58);                // #2A2F3A
    p.themePressed = RGB(54, 60, 74);              // #363C4A
    p.closeHover = RGB(196, 43, 44);               // #C42B2C
    p.closePressed = RGB(166, 35, 36);             // #A62324
    p.scrollTrack = RGB(31, 35, 43);               // #1F232B
    p.scrollThumb = RGB(76, 84, 100);              // #4C5464
    p.scrollThumbHover = RGB(104, 114, 134);       // #687286
    return p;
}

inline ThemePalette MakeLightPalette() {
    // Soft, low-glare matte cool-slate palette: intentionally lower luminance
    // than near-white surfaces to keep long sessions easy on the eyes.
    ThemePalette p{};
    p.windowBackground = RGB(224, 229, 236);       // #E0E5EC calm matte frame
    p.panelBackground = RGB(214, 220, 229);        // #D6DCE5 recessed workspace / dock
    p.cardBackground = RGB(232, 236, 242);         // #E8ECF2 lifted matte card
    p.cardHoverBackground = RGB(237, 241, 246);    // #EDF1F6 gentle hover lift
    p.previewBackground = RGB(214, 220, 229);      // #D6DCE5 workspace well
    p.previewCanvas = RGB(201, 209, 220);          // #C9D1DC muted preview backdrop
    p.settingsSurface = RGB(228, 233, 239);        // #E4E9EF floating popover surface
    p.divider = RGB(192, 200, 212);                // #C0C8D4 hairline divider

    p.normalBorder = RGB(192, 200, 212);           // #C0C8D4
    p.hoverBorder = RGB(144, 156, 174);            // #909CAE
    p.selectedBorder = RGB(37, 99, 235);           // #2563EB crisp cobalt focus
    p.boundBorder = RGB(22, 130, 92);              // #16825C refined emerald
    p.errorBorder = RGB(196, 56, 56);              // #C43838
    p.activeWindowBorder = RGB(172, 181, 194);     // #ACB5C2
    p.inactiveWindowBorder = RGB(194, 202, 213);   // #C2CAD5
    p.previewBorder = RGB(190, 198, 210);          // #BEC6D2
    p.previewInnerBorder = RGB(182, 191, 204);     // #B6BFCC
    p.panelBorder = RGB(190, 198, 210);            // #BEC6D2
    p.inputBorder = RGB(168, 178, 192);            // #A8B2C0

    p.titleText = RGB(34, 41, 52);                 // #222934
    p.mainText = RGB(26, 32, 41);                  // #1A2029
    p.mutedText = RGB(70, 80, 96);                 // #465060 (>= 5.5:1 on panel)
    p.processText = RGB(76, 87, 104);              // #4C5768 (>= 5.7:1 on card)
    p.placeholderText = RGB(74, 84, 100);          // #4A5464
    p.disabledText = RGB(72, 81, 95);              // #48515F (>= 5.0:1 on disabledBtn)

    p.inputBackground = RGB(228, 233, 239);        // #E4E9EF matte inset
    p.inputCapturingBackground = RGB(220, 229, 244);// #DCE5F4 soft focus tint
    p.inputErrorBackground = RGB(240, 224, 226);   // #F0E0E2
    p.keycapBackground = RGB(214, 221, 231);       // #D6DDE7
    p.keycapBorder = RGB(173, 183, 198);           // #ADB7C6
    p.iconWellBackground = RGB(226, 231, 238);     // #E2E7EE
    p.disabledButton = RGB(203, 210, 221);         // #CBD2DD
    p.disabledButtonBorder = RGB(182, 191, 204);   // #B6BFCC
    p.bindButton = RGB(31, 38, 51);                // #1F2633 deep slate primary
    p.bindButtonHover = RGB(45, 54, 71);           // #2D3647
    p.bindButtonPressed = RGB(21, 27, 37);         // #151B25
    p.bindButtonBorder = RGB(31, 38, 51);
    p.buttonText = RGB(236, 240, 246);             // #ECF0F6
    p.unbindButton = RGB(180, 46, 42);             // #B42E2A
    p.unbindButtonHover = RGB(198, 54, 50);        // #C63632
    p.unbindButtonPressed = RGB(153, 38, 35);      // #992623
    p.unbindButtonBorder = RGB(144, 34, 31);
    p.unbindText = RGB(248, 250, 252);             // #F8FAFC
    p.boundBadgeBackground = RGB(202, 226, 216);   // #CAE2D8
    p.boundBadgeBorder = RGB(130, 184, 162);       // #82B8A2
    p.boundBadgeText = RGB(13, 84, 59);            // #0D543B (>= 6.3:1)
    p.warningBackground = RGB(228, 220, 200);      // #E4DCC8
    p.warningBorder = RGB(184, 161, 114);          // #B8A172
    p.warningText = RGB(92, 64, 10);               // #5C400A (>= 6.5:1)
    p.switchOffTrack = RGB(176, 185, 198);         // #B0B9C6
    p.switchOnTrack = RGB(22, 130, 92);            // #16825C
    p.switchThumb = RGB(242, 245, 249);            // #F2F5F9

    p.themeHover = RGB(206, 213, 224);             // #CED5E0
    p.themePressed = RGB(192, 200, 213);           // #C0C8D5
    p.closeHover = RGB(196, 43, 44);               // #C42B2C
    p.closePressed = RGB(166, 35, 36);             // #A62324
    p.scrollTrack = RGB(204, 211, 221);            // #CCD3DD
    p.scrollThumb = RGB(148, 158, 174);            // #949EAE
    p.scrollThumbHover = RGB(118, 129, 146);       // #768192
    return p;
}

inline bool& HighContrastEnabled() { static bool active{}; return active; }
inline ThemePalette& HighContrastPalette() { static ThemePalette p{}; return p; }

inline void RefreshSystemTheme() {
    HIGHCONTRASTW hc{sizeof(hc)};
    HighContrastEnabled() = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) &&
                             (hc.dwFlags & HCF_HIGHCONTRASTON);
    auto& p = HighContrastPalette();
    p = MakeDarkPalette();
    COLORREF bg = GetSysColor(COLOR_WINDOW);
    COLORREF text = GetSysColor(COLOR_WINDOWTEXT);
    COLORREF hi = GetSysColor(COLOR_HIGHLIGHT);
    COLORREF hiText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    COLORREF face = GetSysColor(COLOR_BTNFACE);
    COLORREF gray = GetSysColor(COLOR_GRAYTEXT);

    p.windowBackground = p.panelBackground = p.cardBackground = p.cardHoverBackground =
        p.previewBackground = p.previewCanvas = p.settingsSurface = p.inputBackground =
        p.inputCapturingBackground = p.inputErrorBackground = p.keycapBackground =
        p.iconWellBackground = p.boundBadgeBackground = p.warningBackground =
        p.scrollTrack = bg;
    p.mainText = p.titleText = p.processText = p.mutedText = p.placeholderText =
        p.boundBadgeText = p.warningText = text;
    p.normalBorder = p.panelBorder = p.previewBorder = p.previewInnerBorder =
        p.inputBorder = p.keycapBorder = p.divider = p.activeWindowBorder =
        p.inactiveWindowBorder = p.boundBadgeBorder = p.warningBorder = text;
    p.hoverBorder = p.selectedBorder = p.boundBorder = p.scrollThumb =
        p.scrollThumbHover = p.errorBorder = p.switchOnTrack = hi;
    p.bindButton = p.bindButtonHover = p.bindButtonPressed = p.unbindButton =
        p.unbindButtonHover = p.unbindButtonPressed = p.themeHover = p.themePressed =
        p.closeHover = p.closePressed = hi;
    p.buttonText = p.unbindText = p.switchThumb = hiText;
    p.bindButtonBorder = p.unbindButtonBorder = hi;
    p.disabledButton = p.switchOffTrack = face;
    p.disabledText = p.disabledButtonBorder = gray;
}

inline const ThemePalette& GetThemePalette(bool dark) {
    static const ThemePalette night = MakeDarkPalette();
    static const ThemePalette day = MakeLightPalette();
    return HighContrastEnabled() ? HighContrastPalette() : (dark ? night : day);
}

inline double RelativeLuminance(COLORREF c) {
    auto linear = [](BYTE value) {
        double v = value / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(GetRValue(c)) + 0.7152 * linear(GetGValue(c)) +
           0.0722 * linear(GetBValue(c));
}

inline double ColorContrast(COLORREF a, COLORREF b) {
    double x = RelativeLuminance(a), y = RelativeLuminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

inline bool ValidateThemePalettes() {
    const ThemePalette dark = MakeDarkPalette();
    const ThemePalette light = MakeLightPalette();
    // Ensure light mode stays calmly below blinding near-white luminance.
    if (RelativeLuminance(light.windowBackground) > 0.84 ||
        RelativeLuminance(light.cardBackground) > 0.88) {
        return false;
    }
    for (const auto& p : {dark, light}) {
        if (ColorContrast(p.buttonText, p.bindButton) < 4.5 ||
            ColorContrast(p.unbindText, p.unbindButton) < 4.5 ||
            ColorContrast(p.titleText, p.windowBackground) < 4.5 ||
            ColorContrast(p.mainText, p.cardBackground) < 4.5 ||
            ColorContrast(p.processText, p.cardBackground) < 4.5 ||
            ColorContrast(p.mutedText, p.panelBackground) < 4.5 ||
            ColorContrast(p.boundBadgeText, p.boundBadgeBackground) < 4.5 ||
            ColorContrast(p.warningText, p.warningBackground) < 4.5 ||
            ColorContrast(p.disabledText, p.disabledButton) < 4.5) {
            return false;
        }
    }
    return true;
}

} // namespace windowcue
