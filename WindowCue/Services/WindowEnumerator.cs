using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using WindowCue.Models;
using WindowCue.Native;

namespace WindowCue.Services;

internal sealed class WindowEnumerator
{
    private readonly uint _ownProcessId = (uint)Environment.ProcessId;

    internal IReadOnlyList<WindowDescriptor> Enumerate(bool loadIcons = true)
    {
        var result = new List<WindowDescriptor>();
        IntPtr shell = NativeMethods.GetShellWindow();

        NativeMethods.EnumWindows((hwnd, _) =>
        {
            if (hwnd == shell || !IsUserWindowCandidate(hwnd))
            {
                return true;
            }

            NativeMethods.GetWindowThreadProcessId(hwnd, out uint processId);
            if (processId == 0 || processId == _ownProcessId)
            {
                return true;
            }

            string title = GetTitle(hwnd);
            if (string.IsNullOrWhiteSpace(title))
            {
                return true;
            }

            if (!NativeMethods.GetWindowRect(hwnd, out NativeMethods.NativeRect rect))
            {
                return true;
            }

            // Minimized windows report their small taskbar/iconic rectangle,
            // not the restore rectangle. Applying the normal 80x50 content
            // threshold here made Win+D remove every real application.
            if (!NativeMethods.IsIconic(hwnd) && (rect.Width < 80 || rect.Height < 50))
            {
                return true;
            }

            string className = GetClassName(hwnd);
            string processPath = GetProcessPath(processId);
            // Skip system/elevated helper surfaces that cannot be resolved to
            // an application. This mirrors PowerToys Workspaces and prevents
            // items such as "DWM Notification Window" entering the chooser.
            if (string.IsNullOrWhiteSpace(processPath))
            {
                return true;
            }

            result.Add(new WindowDescriptor
            {
                Handle = hwnd,
                Title = title,
                ClassName = className,
                ProcessId = processId,
                ProcessPath = processPath,
                Icon = loadIcons ? WindowIconProvider.GetIcon(hwnd, processPath) : null
            });
            return true;
        }, IntPtr.Zero);

        return ResolvePackagedWindows(result)
            .OrderBy(item => item.ProcessName, StringComparer.CurrentCultureIgnoreCase)
            .ThenBy(item => item.Title, StringComparer.CurrentCultureIgnoreCase)
            .ToArray();
    }

    internal IntPtr FindBestMatch(BindingSettings settings)
    {
        IReadOnlyList<WindowDescriptor> windows = Enumerate(loadIcons: false);
        if (settings.WindowHandle != 0)
        {
            IntPtr persisted = new(unchecked((long)settings.WindowHandle));
            WindowDescriptor? saved = windows.FirstOrDefault(window =>
                window.Handle == persisted &&
                string.Equals(window.ProcessPath, settings.ProcessPath, StringComparison.OrdinalIgnoreCase) &&
                string.Equals(window.ClassName, settings.ClassName, StringComparison.Ordinal));
            return saved?.Handle ?? IntPtr.Zero;
        }

        // Legacy settings without a saved HWND use best-effort matching once.
        // The caller persists the resolved handle during migration.
        WindowDescriptor[] exact = windows.Where(window =>
            string.Equals(window.ProcessPath, settings.ProcessPath, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(window.ClassName, settings.ClassName, StringComparison.Ordinal) &&
            string.Equals(window.Title, settings.WindowTitle, StringComparison.Ordinal)).ToArray();
        if (exact.Length == 1)
        {
            return exact[0].Handle;
        }
        if (exact.Length > 1)
        {
            return IntPtr.Zero;
        }

        WindowDescriptor[] sameClass = windows.Where(window =>
            string.Equals(window.ProcessPath, settings.ProcessPath, StringComparison.OrdinalIgnoreCase) &&
            string.Equals(window.ClassName, settings.ClassName, StringComparison.Ordinal)).ToArray();
        return sameClass.Length == 1 ? sameClass[0].Handle : IntPtr.Zero;
    }

    private static bool IsUserWindowCandidate(IntPtr hwnd)
    {
        if (!NativeMethods.IsWindowVisible(hwnd) && !NativeMethods.IsIconic(hwnd))
        {
            return false;
        }

        long style = NativeMethods.GetWindowLongPtr(hwnd, NativeMethods.GwlStyle).ToInt64();
        if ((style & NativeMethods.WsChild) != 0 ||
            (style & NativeMethods.WsDisabled) != 0 ||
            NativeMethods.GetAncestor(hwnd, NativeMethods.GaRoot) != hwnd)
        {
            return false;
        }

        long exStyle = NativeMethods.GetWindowLongPtr(hwnd, NativeMethods.GwlExStyle).ToInt64();
        bool isToolWindow = (exStyle & NativeMethods.WsExToolWindow) != 0;
        bool isAppWindow = (exStyle & NativeMethods.WsExAppWindow) != 0;
        bool isNoActivate = (exStyle & NativeMethods.WsExNoActivate) != 0;
        if (isToolWindow && !isAppWindow)
        {
            return false;
        }
        if (isNoActivate && !isAppWindow)
        {
            return false;
        }

        IntPtr owner = NativeMethods.GetWindow(hwnd, NativeMethods.GwOwner);
        // Detached windows and tray-style apps often keep an invisible owner.
        // They are valid task switcher entries; only suppress the owned window
        // while its owner is itself visible.
        if (owner != IntPtr.Zero && NativeMethods.IsWindowVisible(owner) && !isAppWindow)
        {
            return false;
        }

        string className = GetClassName(hwnd);
        if (NativeMethods.GetProp(hwnd, "ITaskList_Deleted") != IntPtr.Zero)
        {
            return false;
        }

        if (string.Equals(className, "ApplicationFrameWindow", StringComparison.Ordinal) &&
            NativeMethods.DwmGetWindowAttribute(
                hwnd,
                NativeMethods.DwmwaCloaked,
                out int cloakState,
                sizeof(int)) == 0 &&
            cloakState == NativeMethods.DwmCloakedShell)
        {
            return false;
        }

        // Do not reject DWM_CLOAKED_SHELL here. Show Desktop (Win+D) can cloak
        // genuine taskbar windows without destroying their HWND, so treating
        // shell-cloaked as hidden makes the complete list disappear.
        return true;
    }

    private static IEnumerable<WindowDescriptor> ResolvePackagedWindows(
        IReadOnlyList<WindowDescriptor> windows)
    {
        // Packaged apps can expose both an ApplicationFrameHost top-level HWND
        // and a same-title app-process HWND. Keep the frame HWND (the taskbar
        // surface) but use the app process path for stable identity and binding.
        var skipped = new HashSet<IntPtr>();
        foreach (WindowDescriptor frame in windows.Where(IsApplicationFrameHost))
        {
            WindowDescriptor? appWindow = windows.FirstOrDefault(candidate =>
                candidate.Handle != frame.Handle &&
                candidate.ProcessId != frame.ProcessId &&
                !IsApplicationFrameHost(candidate) &&
                string.Equals(candidate.Title, frame.Title, StringComparison.Ordinal));

            if (appWindow is null)
            {
                yield return frame;
                continue;
            }

            skipped.Add(appWindow.Handle);
            yield return new WindowDescriptor
            {
                Handle = frame.Handle,
                Title = frame.Title,
                ClassName = frame.ClassName,
                ProcessId = appWindow.ProcessId,
                ProcessPath = appWindow.ProcessPath,
                Icon = frame.Icon ?? appWindow.Icon
            };
        }

        foreach (WindowDescriptor window in windows)
        {
            if (!IsApplicationFrameHost(window) &&
                !skipped.Contains(window.Handle) &&
                !string.Equals(
                    window.ClassName,
                    "Windows.UI.Core.CoreWindow",
                    StringComparison.Ordinal))
            {
                yield return window;
            }
        }
    }

    private static bool IsApplicationFrameHost(WindowDescriptor window)
    {
        return string.Equals(
            System.IO.Path.GetFileName(window.ProcessPath),
            "ApplicationFrameHost.exe",
            StringComparison.OrdinalIgnoreCase);
    }

    private static string GetTitle(IntPtr hwnd)
    {
        int length = Math.Clamp(NativeMethods.GetWindowTextLength(hwnd) + 1, 2, 2048);
        var text = new StringBuilder(length);
        NativeMethods.GetWindowText(hwnd, text, text.Capacity);
        return text.ToString().Trim();
    }

    private static string GetClassName(IntPtr hwnd)
    {
        var className = new StringBuilder(256);
        NativeMethods.GetClassName(hwnd, className, className.Capacity);
        return className.ToString();
    }

    private static string GetProcessPath(uint processId)
    {
        IntPtr process = NativeMethods.OpenProcess(
            NativeMethods.ProcessQueryLimitedInformation,
            false,
            processId);
        if (process == IntPtr.Zero)
        {
            return string.Empty;
        }

        try
        {
            int size = 1024;
            var path = new StringBuilder(size);
            return NativeMethods.QueryFullProcessImageName(process, 0, path, ref size)
                ? path.ToString()
                : string.Empty;
        }
        finally
        {
            NativeMethods.CloseHandle(process);
        }
    }
}
