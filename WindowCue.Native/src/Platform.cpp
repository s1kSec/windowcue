#include "WindowCueNative.h"

#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace windowcue {
namespace {

constexpr LONG_PTR kWsChild = 0x40000000L;
constexpr LONG_PTR kWsDisabled = 0x08000000L;
constexpr LONG_PTR kWsExToolWindow = 0x00000080L;
constexpr LONG_PTR kWsExAppWindow = 0x00040000L;
constexpr LONG_PTR kWsExNoActivate = 0x08000000L;
constexpr DWORD kProcessQueryLimitedInformation = 0x1000;
constexpr DWORD kDwmwaCloaked = 14;
constexpr int kDwmCloakedShell = 2;

std::wstring WindowText(HWND hwnd) {
    int length = std::clamp(GetWindowTextLengthW(hwnd) + 1, 2, 2048);
    std::wstring value(static_cast<size_t>(length), L'\0');
    int written = GetWindowTextW(hwnd, value.data(), length);
    value.resize(written > 0 ? static_cast<size_t>(written) : 0);
    while (!value.empty() && iswspace(value.back())) value.pop_back();
    size_t start = 0;
    while (start < value.size() && iswspace(value[start])) ++start;
    return start == 0 ? value : value.substr(start);
}

std::wstring WindowClass(HWND hwnd) {
    wchar_t value[256]{};
    int written = GetClassNameW(hwnd, value, static_cast<int>(std::size(value)));
    return written > 0 ? std::wstring(value, static_cast<size_t>(written)) : std::wstring();
}

std::wstring ProcessPath(DWORD processId) {
    HANDLE process = OpenProcess(kProcessQueryLimitedInformation, FALSE, processId);
    if (!process) return {};
    wchar_t value[32768]{};
    DWORD length = static_cast<DWORD>(std::size(value));
    BOOL ok = QueryFullProcessImageNameW(process, 0, value, &length);
    CloseHandle(process);
    return ok ? std::wstring(value, static_cast<size_t>(length)) : std::wstring();
}

bool EqualsInsensitive(const std::wstring& left, const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

bool IsApplicationFrameHost(const WindowInfo& item) {
    return EqualsInsensitive(FileNameFromPath(item.processPath), L"ApplicationFrameHost.exe");
}

bool IsCandidate(HWND hwnd) {
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) return false;

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & kWsChild) != 0 || (style & kWsDisabled) != 0 ||
        GetAncestor(hwnd, GA_ROOT) != hwnd) {
        return false;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    bool toolWindow = (exStyle & kWsExToolWindow) != 0;
    bool appWindow = (exStyle & kWsExAppWindow) != 0;
    bool noActivate = (exStyle & kWsExNoActivate) != 0;
    if ((toolWindow || noActivate) && !appWindow) return false;

    HWND owner = GetWindow(hwnd, GW_OWNER);
    if (owner && IsWindowVisible(owner) && !appWindow) return false;
    if (GetPropW(hwnd, L"ITaskList_Deleted")) return false;

    std::wstring className = WindowClass(hwnd);
    if (className == L"ApplicationFrameWindow") {
        int cloakState = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(
                hwnd, kDwmwaCloaked, &cloakState, sizeof(cloakState))) &&
            cloakState == kDwmCloakedShell) {
            return false;
        }
    }
    return true;
}

HICON CopyWindowIcon(HWND hwnd, const std::wstring& processPath) {
    const WPARAM sizes[] = {ICON_BIG, ICON_SMALL2, ICON_SMALL};
    for (WPARAM size : sizes) {
        DWORD_PTR result = 0;
        SendMessageTimeoutW(
            hwnd, WM_GETICON, size, 0, SMTO_ABORTIFHUNG, 100, &result);
        if (result) return CopyIcon(reinterpret_cast<HICON>(result));
    }

    HICON icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON));
    if (!icon) icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM));
    if (icon) return CopyIcon(icon);

    if (processPath.empty()) return nullptr;
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(
            processPath.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
            SHGFI_ICON | SHGFI_LARGEICON)) {
        return nullptr;
    }
    return info.hIcon; // SHGetFileInfo returns an owned icon.
}

struct EnumContext {
    DWORD ownProcessId{};
    bool loadIcons{};
    HWND shell{};
    std::vector<WindowInfo> items;
};

BOOL CALLBACK EnumCallback(HWND hwnd, LPARAM data) {
    auto& context = *reinterpret_cast<EnumContext*>(data);
    if (hwnd == context.shell || !IsCandidate(hwnd)) return TRUE;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (!processId || processId == context.ownProcessId) return TRUE;

    std::wstring title = WindowText(hwnd);
    if (title.empty()) return TRUE;

    RECT bounds{};
    if (!GetWindowRect(hwnd, &bounds)) return TRUE;
    if (!IsIconic(hwnd) &&
        ((bounds.right - bounds.left) < 80 || (bounds.bottom - bounds.top) < 50)) {
        return TRUE;
    }

    std::wstring path = ProcessPath(processId);
    if (path.empty()) return TRUE;

    WindowInfo item;
    item.handle = hwnd;
    item.title = std::move(title);
    item.className = WindowClass(hwnd);
    item.processId = processId;
    item.processPath = std::move(path);
    if (context.loadIcons) item.icon = CopyWindowIcon(hwnd, item.processPath);
    context.items.push_back(std::move(item));
    return TRUE;
}

std::wstring SettingsDirectory() {
    DWORD overrideLength = GetEnvironmentVariableW(L"WINDOWCUE_SETTINGS_DIR", nullptr, 0);
    if (overrideLength > 1) {
        std::wstring overridePath(static_cast<size_t>(overrideLength), L'\0');
        DWORD written = GetEnvironmentVariableW(
            L"WINDOWCUE_SETTINGS_DIR", overridePath.data(), overrideLength);
        if (written > 0 && written < overrideLength) {
            overridePath.resize(written);
            return overridePath;
        }
    }
    wchar_t* localAppData = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        result = localAppData;
        CoTaskMemFree(localAppData);
        result += L"\\WindowCue";
    }
    return result;
}

bool ReadUtf8File(const std::wstring& path, std::wstring& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= 4 * 1024 * 1024;
    std::string bytes;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) &&
                               read == bytes.size());
    }
    CloseHandle(file);
    if (!ok) return false;
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    if (bytes.empty()) {
        text.clear();
        return true;
    }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                                     static_cast<int>(bytes.size()), nullptr, 0);
    if (length <= 0) return false;
    text.resize(static_cast<size_t>(length));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
                               static_cast<int>(bytes.size()), text.data(), length) == length;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (length < 0) return false;
    std::string bytes(static_cast<size_t>(length), '\0');
    if (length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                            bytes.data(), length, nullptr, nullptr);
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = bytes.empty() || (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                                         &written, nullptr) && written == bytes.size());
    CloseHandle(file);
    return ok;
}

size_t SkipSpace(const std::wstring& text, size_t position) {
    while (position < text.size() && iswspace(text[position])) ++position;
    return position;
}

bool ParseJsonString(const std::wstring& text, size_t& position, std::wstring& value) {
    position = SkipSpace(text, position);
    if (position >= text.size() || text[position] != L'"') return false;
    ++position;
    value.clear();
    while (position < text.size()) {
        wchar_t ch = text[position++];
        if (ch == L'"') return true;
        if (ch != L'\\') {
            value.push_back(ch);
            continue;
        }
        if (position >= text.size()) return false;
        wchar_t escaped = text[position++];
        switch (escaped) {
        case L'"': value.push_back(L'"'); break;
        case L'\\': value.push_back(L'\\'); break;
        case L'/': value.push_back(L'/'); break;
        case L'b': value.push_back(L'\b'); break;
        case L'f': value.push_back(L'\f'); break;
        case L'n': value.push_back(L'\n'); break;
        case L'r': value.push_back(L'\r'); break;
        case L't': value.push_back(L'\t'); break;
        case L'u': {
            if (position + 4 > text.size()) return false;
            unsigned valueCode = 0;
            for (int i = 0; i < 4; ++i) {
                wchar_t digit = text[position++];
                valueCode <<= 4;
                if (digit >= L'0' && digit <= L'9') valueCode += digit - L'0';
                else if (digit >= L'a' && digit <= L'f') valueCode += digit - L'a' + 10;
                else if (digit >= L'A' && digit <= L'F') valueCode += digit - L'A' + 10;
                else return false;
            }
            value.push_back(static_cast<wchar_t>(valueCode));
            break;
        }
        default: return false;
        }
    }
    return false;
}

bool ExtractString(const std::wstring& object, const wchar_t* name, std::wstring& value) {
    std::wstring key = L"\"" + std::wstring(name) + L"\"";
    size_t position = object.find(key);
    if (position == std::wstring::npos) return false;
    position = object.find(L':', position + key.size());
    if (position == std::wstring::npos) return false;
    ++position;
    return ParseJsonString(object, position, value);
}

bool ExtractUInt(const std::wstring& object, const wchar_t* name, UINT& value) {
    std::wstring key = L"\"" + std::wstring(name) + L"\"";
    size_t position = object.find(key);
    if (position == std::wstring::npos) return false;
    position = object.find(L':', position + key.size());
    if (position == std::wstring::npos) return false;
    position = SkipSpace(object, position + 1);
    wchar_t* end = nullptr;
    unsigned long parsed = wcstoul(object.c_str() + position, &end, 10);
    if (end == object.c_str() + position) return false;
    value = static_cast<UINT>(parsed);
    return true;
}

bool ExtractUInt64(const std::wstring& object, const wchar_t* name,
                   std::uint64_t& value) {
    std::wstring key = L"\"" + std::wstring(name) + L"\"";
    size_t position = object.find(key);
    if (position == std::wstring::npos) return false;
    position = object.find(L':', position + key.size());
    if (position == std::wstring::npos) return false;
    position = SkipSpace(object, position + 1);
    wchar_t* end = nullptr;
    unsigned long long parsed = _wcstoui64(object.c_str() + position, &end, 10);
    if (end == object.c_str() + position) return false;
    value = static_cast<std::uint64_t>(parsed);
    return true;
}

bool ExtractBool(const std::wstring& object, const wchar_t* name, bool& value) {
    std::wstring key = L"\"" + std::wstring(name) + L"\"";
    size_t position = object.find(key);
    if (position == std::wstring::npos) return false;
    position = object.find(L':', position + key.size());
    if (position == std::wstring::npos) return false;
    position = SkipSpace(object, position + 1);
    if (object.compare(position, 4, L"true") == 0) {
        value = true;
        return true;
    }
    if (object.compare(position, 5, L"false") == 0) {
        value = false;
        return true;
    }
    return false;
}

std::vector<BindingSettings> ParseSettings(const std::wstring& json) {
    std::vector<BindingSettings> result;
    size_t position = 0;
    while ((position = json.find(L'{', position)) != std::wstring::npos) {
        bool inString = false;
        bool escaped = false;
        int depth = 0;
        size_t end = position;
        for (; end < json.size(); ++end) {
            wchar_t ch = json[end];
            if (inString) {
                if (escaped) escaped = false;
                else if (ch == L'\\') escaped = true;
                else if (ch == L'"') inString = false;
                continue;
            }
            if (ch == L'"') inString = true;
            else if (ch == L'{') ++depth;
            else if (ch == L'}' && --depth == 0) break;
        }
        if (end >= json.size()) break;
        std::wstring object = json.substr(position, end - position + 1);
        BindingSettings item;
        ExtractString(object, L"ProcessPath", item.processPath);
        ExtractString(object, L"WindowTitle", item.windowTitle);
        ExtractString(object, L"ClassName", item.className);
        ExtractString(object, L"HotkeyText", item.hotkeyText);
        ExtractUInt64(object, L"WindowHandle", item.windowHandle);
        ExtractUInt(object, L"Modifiers", item.modifiers);
        ExtractUInt(object, L"VirtualKey", item.virtualKey);
        ExtractBool(object, L"IsWindowCue", item.isWindowCue);
        if (!item.processPath.empty() && item.modifiers && item.virtualKey) {
            result.push_back(std::move(item));
        }
        position = end + 1;
    }
    return result;
}

std::wstring EscapeJson(const std::wstring& value) {
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        switch (ch) {
        case L'"': escaped += L"\\\""; break;
        case L'\\': escaped += L"\\\\"; break;
        case L'\b': escaped += L"\\b"; break;
        case L'\f': escaped += L"\\f"; break;
        case L'\n': escaped += L"\\n"; break;
        case L'\r': escaped += L"\\r"; break;
        case L'\t': escaped += L"\\t"; break;
        default:
            if (ch < 0x20) {
                wchar_t buffer[7]{};
                swprintf_s(buffer, L"\\u%04X", static_cast<unsigned>(ch));
                escaped += buffer;
            } else {
                escaped.push_back(ch);
            }
        }
    }
    return escaped;
}

} // namespace

std::wstring FileNameFromPath(const std::wstring& path) {
    const wchar_t* name = PathFindFileNameW(path.c_str());
    return name ? std::wstring(name) : path;
}

std::vector<WindowInfo> EnumerateWindows(bool loadIcons, DWORD ownProcessId) {
    EnumContext context;
    context.ownProcessId = ownProcessId;
    context.loadIcons = loadIcons;
    context.shell = GetShellWindow();
    EnumWindows(EnumCallback, reinterpret_cast<LPARAM>(&context));

    std::vector<bool> skip(context.items.size(), false);
    for (size_t i = 0; i < context.items.size(); ++i) {
        WindowInfo& frame = context.items[i];
        if (!IsApplicationFrameHost(frame)) continue;
        for (size_t j = 0; j < context.items.size(); ++j) {
            WindowInfo& candidate = context.items[j];
            if (i == j || candidate.processId == frame.processId ||
                IsApplicationFrameHost(candidate) || candidate.title != frame.title) {
                continue;
            }
            frame.processId = candidate.processId;
            frame.processPath = candidate.processPath;
            if (!frame.icon && candidate.icon) {
                frame.icon = candidate.icon;
                candidate.icon = nullptr;
            }
            skip[j] = true;
            break;
        }
    }

    std::vector<WindowInfo> result;
    result.reserve(context.items.size());
    for (size_t i = 0; i < context.items.size(); ++i) {
        WindowInfo& item = context.items[i];
        bool coreWindow = item.className == L"Windows.UI.Core.CoreWindow";
        if (!skip[i] && !coreWindow) {
            result.push_back(std::move(item));
            item.icon = nullptr;
        }
        if (item.icon) {
            DestroyIcon(item.icon);
            item.icon = nullptr;
        }
    }

    std::sort(result.begin(), result.end(), [](const WindowInfo& left, const WindowInfo& right) {
        int processOrder = _wcsicmp(FileNameFromPath(left.processPath).c_str(),
                                    FileNameFromPath(right.processPath).c_str());
        return processOrder != 0 ? processOrder < 0 : _wcsicmp(left.title.c_str(), right.title.c_str()) < 0;
    });
    return result;
}

void ReleaseWindows(std::vector<WindowInfo>& windows) {
    for (WindowInfo& item : windows) {
        if (item.thumbnail) DwmUnregisterThumbnail(item.thumbnail);
        if (item.icon) DestroyIcon(item.icon);
        item.thumbnail = nullptr;
        item.icon = nullptr;
    }
    windows.clear();
}

HWND FindBestMatch(const BindingSettings& settings, DWORD ownProcessId) {
    std::vector<WindowInfo> windows = EnumerateWindows(false, ownProcessId);

    // HWND remains stable while the target window is alive, including across a
    // WindowCue restart. Validate it against the persisted executable and class
    // before accepting it so a recycled handle can never redirect a hotkey.
    if (settings.windowHandle) {
        HWND persisted = reinterpret_cast<HWND>(
            static_cast<std::uintptr_t>(settings.windowHandle));
        for (const WindowInfo& item : windows) {
            if (item.handle != persisted) continue;
            bool matches = EqualsInsensitive(item.processPath, settings.processPath) &&
                           item.className == settings.className;
            HWND exact = matches ? item.handle : nullptr;
            ReleaseWindows(windows);
            return exact;
        }
        ReleaseWindows(windows);
        return nullptr;
    }

    // Legacy settings did not contain an HWND. Migrate only when the metadata
    // identifies exactly one candidate; choosing the first of duplicate titles
    // would silently redirect the shortcut to a sibling window.
    HWND exactTitle = nullptr;
    size_t exactTitleCount = 0;
    HWND sameClass = nullptr;
    size_t sameClassCount = 0;
    for (const WindowInfo& item : windows) {
        if (!EqualsInsensitive(item.processPath, settings.processPath) ||
            item.className != settings.className) {
            continue;
        }
        sameClass = item.handle;
        ++sameClassCount;
        if (item.title == settings.windowTitle) {
            exactTitle = item.handle;
            ++exactTitleCount;
        }
    }
    ReleaseWindows(windows);
    if (exactTitleCount == 1) return exactTitle;
    if (exactTitleCount > 1) return nullptr;
    return sameClassCount == 1 ? sameClass : nullptr;
}

void ActivateWindow(HWND target) {
    if (!target || !IsWindow(target)) return;
    ShowWindowAsync(target, IsIconic(target) ? SW_RESTORE : SW_SHOW);
    Sleep(35);
    SetWindowPos(target, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(target);
    if (SetForegroundWindow(target) && GetForegroundWindow() == target) return;

    DWORD targetThread = GetWindowThreadProcessId(target, nullptr);
    DWORD currentThread = GetCurrentThreadId();
    HWND foreground = GetForegroundWindow();
    DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    bool attachedTarget = currentThread != targetThread &&
                          AttachThreadInput(currentThread, targetThread, TRUE);
    bool attachedForeground = foregroundThread && foregroundThread != currentThread &&
                              foregroundThread != targetThread &&
                              AttachThreadInput(currentThread, foregroundThread, TRUE);
    BringWindowToTop(target);
    SetForegroundWindow(target);
    if (attachedForeground) AttachThreadInput(currentThread, foregroundThread, FALSE);
    if (attachedTarget) AttachThreadInput(currentThread, targetThread, FALSE);
}

bool ToggleWindow(HWND target) {
    if (!target || !IsWindow(target)) return false;
    if (GetForegroundWindow() == target && !IsIconic(target)) {
        return ShowWindowAsync(target, SW_MINIMIZE) != FALSE;
    }
    ActivateWindow(target);
    return true;
}

std::vector<BindingSettings> LoadSettings() {
    std::wstring directory = SettingsDirectory();
    if (directory.empty()) return {};
    std::wstring json;
    if (ReadUtf8File(directory + L"\\bindings.json", json)) return ParseSettings(json);
    if (ReadUtf8File(directory + L"\\binding.json", json)) return ParseSettings(json);
    return {};
}

bool SaveSettings(const std::vector<ActiveBinding>& bindings) {
    std::wstring directory = SettingsDirectory();
    if (directory.empty()) return false;
    CreateDirectoryW(directory.c_str(), nullptr);
    std::wstring current = directory + L"\\bindings.json";
    std::wstring legacy = directory + L"\\binding.json";
    if (bindings.empty()) {
        DeleteFileW(current.c_str());
        DeleteFileW(legacy.c_str());
        return true;
    }

    std::wostringstream output;
    output << L"[\n";
    for (size_t i = 0; i < bindings.size(); ++i) {
        const BindingSettings& item = bindings[i].settings;
        auto handleValue = static_cast<unsigned long long>(
            reinterpret_cast<std::uintptr_t>(bindings[i].target));
        output << L"  {\n"
               << L"    \"ProcessPath\": \"" << EscapeJson(item.processPath) << L"\",\n"
               << L"    \"WindowTitle\": \"" << EscapeJson(item.windowTitle) << L"\",\n"
               << L"    \"ClassName\": \"" << EscapeJson(item.className) << L"\",\n"
               << L"    \"WindowHandle\": " << handleValue << L",\n"
               << L"    \"IsWindowCue\": " << (item.isWindowCue ? L"true" : L"false") << L",\n"
               << L"    \"Modifiers\": " << item.modifiers << L",\n"
               << L"    \"VirtualKey\": " << item.virtualKey << L",\n"
               << L"    \"HotkeyText\": \"" << EscapeJson(item.hotkeyText) << L"\"\n"
               << L"  }" << (i + 1 < bindings.size() ? L"," : L"") << L"\n";
    }
    output << L"]\n";
    bool saved = WriteUtf8File(current, output.str());
    if (saved) DeleteFileW(legacy.c_str());
    return saved;
}

bool LoadDarkModePreference() {
    std::wstring directory = SettingsDirectory();
    if (directory.empty()) return true;
    std::wstring value;
    if (!ReadUtf8File(directory + L"\\appearance.txt", value)) return true;
    return value.find(L"light") == std::wstring::npos;
}

bool SaveDarkModePreference(bool darkMode) {
    std::wstring directory = SettingsDirectory();
    if (directory.empty()) return false;
    CreateDirectoryW(directory.c_str(), nullptr);
    return WriteUtf8File(directory + L"\\appearance.txt",
                         darkMode ? L"dark\n" : L"light\n");
}

bool IsSelfHotkeyInitialized() {
    std::wstring directory = SettingsDirectory();
    std::wstring value;
    return !directory.empty() &&
        ReadUtf8File(directory + L"\\self-hotkey-initialized.txt", value) &&
        value == L"initialized\n";
}

bool MarkSelfHotkeyInitialized() {
    std::wstring directory = SettingsDirectory();
    if (directory.empty()) return false;
    CreateDirectoryW(directory.c_str(), nullptr);
    return WriteUtf8File(directory + L"\\self-hotkey-initialized.txt", L"initialized\n");
}

std::wstring BuildHotkeyText(UINT modifiers, UINT virtualKey) {
    std::wstring text;
    auto append = [&text](const wchar_t* part) {
        if (!text.empty()) text += L" + ";
        text += part;
    };
    if (modifiers & MOD_CONTROL) append(L"Ctrl");
    if (modifiers & MOD_ALT) append(L"Alt");
    if (modifiers & MOD_SHIFT) append(L"Shift");
    if (modifiers & MOD_WIN) append(L"Win");

    wchar_t key[64]{};
    if ((virtualKey >= L'A' && virtualKey <= L'Z') ||
        (virtualKey >= L'0' && virtualKey <= L'9')) {
        key[0] = static_cast<wchar_t>(virtualKey);
        key[1] = 0;
    } else if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
        swprintf_s(key, L"F%u", virtualKey - VK_F1 + 1);
    } else {
        UINT scan = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
        LONG parameter = static_cast<LONG>(scan << 16);
        if (virtualKey == VK_LEFT || virtualKey == VK_UP || virtualKey == VK_RIGHT ||
            virtualKey == VK_DOWN || virtualKey == VK_PRIOR || virtualKey == VK_NEXT ||
            virtualKey == VK_END || virtualKey == VK_HOME || virtualKey == VK_INSERT ||
            virtualKey == VK_DELETE || virtualKey == VK_DIVIDE || virtualKey == VK_NUMLOCK) {
            parameter |= 1 << 24;
        }
        if (!GetKeyNameTextW(parameter, key, static_cast<int>(std::size(key)))) {
            swprintf_s(key, L"0x%02X", virtualKey);
        }
    }
    append(key);
    return text;
}

} // namespace windowcue
