using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows.Interop;
using WindowCue.Models;
using WindowCue.Native;

namespace WindowCue.Services;

internal sealed class HotkeyManager : IDisposable
{
    private readonly IntPtr _windowHandle;
    private readonly HwndSource _source;
    private readonly HashSet<int> _registeredIds = new();

    internal event EventHandler<HotkeyPressedEventArgs>? Pressed;

    internal HotkeyManager(IntPtr windowHandle)
    {
        _windowHandle = windowHandle;
        _source = HwndSource.FromHwnd(windowHandle)
                  ?? throw new InvalidOperationException("无法创建窗口消息源。");
        _source.AddHook(WndProc);
    }

    internal void Register(int hotkeyId, HotkeyGesture gesture)
    {
        Unregister(hotkeyId);
        uint modifiers = gesture.Modifiers | NativeMethods.ModNoRepeat;
        if (!NativeMethods.RegisterHotKey(
                _windowHandle,
                hotkeyId,
                modifiers,
                gesture.VirtualKey))
        {
            throw new Win32Exception(
                Marshal.GetLastWin32Error(),
                Strings.ShortcutOccupied(gesture.DisplayText));
        }
        _registeredIds.Add(hotkeyId);
    }

    internal void Unregister(int hotkeyId)
    {
        if (!_registeredIds.Remove(hotkeyId))
        {
            return;
        }
        NativeMethods.UnregisterHotKey(_windowHandle, hotkeyId);
    }

    internal void UnregisterAll()
    {
        foreach (int hotkeyId in new List<int>(_registeredIds))
        {
            Unregister(hotkeyId);
        }
    }

    private IntPtr WndProc(IntPtr hwnd, int message, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (message != NativeMethods.WmHotkey)
        {
            return IntPtr.Zero;
        }

        long rawHotkeyId = wParam.ToInt64();
        if (rawHotkeyId is < int.MinValue or > int.MaxValue)
        {
            return IntPtr.Zero;
        }

        int hotkeyId = (int)rawHotkeyId;
        if (_registeredIds.Contains(hotkeyId))
        {
            handled = true;
            Pressed?.Invoke(this, new HotkeyPressedEventArgs(hotkeyId));
        }
        return IntPtr.Zero;
    }

    public void Dispose()
    {
        UnregisterAll();
        _source.RemoveHook(WndProc);
    }
}

internal sealed class HotkeyPressedEventArgs(int hotkeyId) : EventArgs
{
    internal int HotkeyId { get; } = hotkeyId;
}
