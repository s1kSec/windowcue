using System;
using System.Threading;
using WindowCue.Native;

namespace WindowCue.Services;

internal static class WindowActivator
{
    internal static bool Toggle(IntPtr hwnd)
    {
        if (hwnd == IntPtr.Zero || !NativeMethods.IsWindow(hwnd))
        {
            return false;
        }

        if (NativeMethods.GetForegroundWindow() == hwnd && !NativeMethods.IsIconic(hwnd))
        {
            return NativeMethods.ShowWindowAsync(hwnd, NativeMethods.SwMinimize);
        }

        Activate(hwnd);
        return true;
    }

    internal static void Activate(IntPtr hwnd)
    {
        NativeMethods.ShowWindowAsync(
            hwnd,
            NativeMethods.IsIconic(hwnd) ? NativeMethods.SwRestore : NativeMethods.SwShow);
        Thread.Sleep(35);
        NativeMethods.SetWindowPos(
            hwnd,
            NativeMethods.HwndTop,
            0,
            0,
            0,
            0,
            NativeMethods.SwpNoMove | NativeMethods.SwpNoSize | NativeMethods.SwpShowWindow);
        NativeMethods.BringWindowToTop(hwnd);

        if (NativeMethods.SetForegroundWindow(hwnd) && NativeMethods.GetForegroundWindow() == hwnd)
        {
            return;
        }

        uint targetThread = NativeMethods.GetWindowThreadProcessId(hwnd, out _);
        uint currentThread = NativeMethods.GetCurrentThreadId();
        IntPtr foregroundWindow = NativeMethods.GetForegroundWindow();
        uint foregroundThread = foregroundWindow == IntPtr.Zero
            ? 0
            : NativeMethods.GetWindowThreadProcessId(foregroundWindow, out _);

        bool attachedTarget = currentThread != targetThread &&
                              NativeMethods.AttachThreadInput(currentThread, targetThread, true);
        bool attachedForeground = foregroundThread != 0 &&
                                  foregroundThread != currentThread &&
                                  foregroundThread != targetThread &&
                                  NativeMethods.AttachThreadInput(currentThread, foregroundThread, true);
        try
        {
            NativeMethods.BringWindowToTop(hwnd);
            NativeMethods.SetForegroundWindow(hwnd);
        }
        finally
        {
            if (attachedForeground)
            {
                NativeMethods.AttachThreadInput(currentThread, foregroundThread, false);
            }
            if (attachedTarget)
            {
                NativeMethods.AttachThreadInput(currentThread, targetThread, false);
            }
        }
    }
}
