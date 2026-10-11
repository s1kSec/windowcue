using System.Collections.Generic;
using System.Windows.Input;
using WindowCue.Native;

namespace WindowCue.Models;

internal readonly record struct HotkeyGesture(uint Modifiers, uint VirtualKey, string DisplayText)
{
    internal static bool TryFromKeyboard(System.Windows.Input.KeyEventArgs e, out HotkeyGesture gesture)
    {
        gesture = default;
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;
        if (key is Key.LeftCtrl or Key.RightCtrl or Key.LeftAlt or Key.RightAlt or
            Key.LeftShift or Key.RightShift or Key.LWin or Key.RWin or Key.None)
        {
            return false;
        }

        ModifierKeys keys = Keyboard.Modifiers;
        uint modifiers = 0;
        var names = new List<string>();
        if (keys.HasFlag(ModifierKeys.Control))
        {
            modifiers |= NativeMethods.ModControl;
            names.Add("Ctrl");
        }
        if (keys.HasFlag(ModifierKeys.Alt))
        {
            modifiers |= NativeMethods.ModAlt;
            names.Add("Alt");
        }
        if (keys.HasFlag(ModifierKeys.Shift))
        {
            modifiers |= NativeMethods.ModShift;
            names.Add("Shift");
        }
        if (keys.HasFlag(ModifierKeys.Windows))
        {
            modifiers |= NativeMethods.ModWin;
            names.Add("Win");
        }

        if (modifiers == 0)
        {
            return false;
        }

        int vk = KeyInterop.VirtualKeyFromKey(key);
        if (vk <= 0)
        {
            return false;
        }

        names.Add(FormatKey(key));
        gesture = new HotkeyGesture(modifiers, (uint)vk, string.Join(" + ", names));
        return true;
    }

    private static string FormatKey(Key key)
    {
        if (key >= Key.D0 && key <= Key.D9)
        {
            return ((int)(key - Key.D0)).ToString();
        }
        if (key >= Key.NumPad0 && key <= Key.NumPad9)
        {
            return "Num " + (int)(key - Key.NumPad0);
        }
        return key.ToString();
    }
}
