#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace windowcue {

constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kThumbnailTimer = 1;
constexpr UINT_PTR kRefreshTimer = 2;
constexpr int kFirstHotkeyId = 0x5748;

struct WindowInfo {
    HWND handle{};
    std::wstring title;
    std::wstring className;
    DWORD processId{};
    std::wstring processPath;
    HICON icon{};          // Owned by the WindowInfo list.
    HTHUMBNAIL thumbnail{};
    RECT appliedThumbnailRect{};
    RECT appliedThumbnailSourceRect{};
    bool thumbnailPropertiesValid{};
    bool thumbnailVisible{};
    bool thumbnailSourceIconic{};
    bool thumbnailSourceForeground{};
    RECT cardRect{};
    RECT previewRect{};
    bool bound{};
    bool isWindowCue{};
};

struct BindingSettings {
    std::wstring processPath;
    std::wstring windowTitle;
    std::wstring className;
    std::uint64_t windowHandle{};
    UINT modifiers{};
    UINT virtualKey{};
    std::wstring hotkeyText;
    bool isWindowCue{};
};

struct ActiveBinding {
    int hotkeyId{};
    BindingSettings settings;
    HWND target{};
    bool registered{};
};

std::vector<WindowInfo> EnumerateWindows(bool loadIcons, DWORD ownProcessId);
void ReleaseWindows(std::vector<WindowInfo>& windows);
HWND FindBestMatch(const BindingSettings& settings, DWORD ownProcessId);

bool ToggleWindow(HWND target);
void ActivateWindow(HWND target);

std::vector<BindingSettings> LoadSettings();
bool SaveSettings(const std::vector<ActiveBinding>& bindings);
bool LoadDarkModePreference();
bool SaveDarkModePreference(bool darkMode);
bool IsSelfHotkeyInitialized();
bool MarkSelfHotkeyInitialized();

HBITMAP RenderSvgResource(HINSTANCE instance, int resourceId, int pixelSize);

std::wstring FileNameFromPath(const std::wstring& path);
std::wstring BuildHotkeyText(UINT modifiers, UINT virtualKey);

} // namespace windowcue
