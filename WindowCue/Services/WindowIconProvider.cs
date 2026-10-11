using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using WindowCue.Native;

namespace WindowCue.Services;

internal static class WindowIconProvider
{
    internal static ImageSource? GetIcon(IntPtr hwnd, string processPath)
    {
        IntPtr icon = GetWindowIcon(hwnd);
        if (icon != IntPtr.Zero)
        {
            return ConvertIcon(icon);
        }

        if (string.IsNullOrWhiteSpace(processPath))
        {
            return null;
        }

        NativeMethods.ShFileInfo info;
        IntPtr result = NativeMethods.SHGetFileInfo(
            processPath,
            NativeMethods.FileAttributeNormal,
            out info,
            (uint)Marshal.SizeOf<NativeMethods.ShFileInfo>(),
            NativeMethods.ShgfiIcon | NativeMethods.ShgfiLargeIcon);

        if (result == IntPtr.Zero || info.Icon == IntPtr.Zero)
        {
            return null;
        }

        try
        {
            return ConvertIcon(info.Icon);
        }
        finally
        {
            NativeMethods.DestroyIcon(info.Icon);
        }
    }

    private static IntPtr GetWindowIcon(IntPtr hwnd)
    {
        int[] sizes = { NativeMethods.IconBig, NativeMethods.IconSmall2, NativeMethods.IconSmall };
        foreach (int size in sizes)
        {
            NativeMethods.SendMessageTimeout(
                hwnd,
                NativeMethods.WmGetIcon,
                (IntPtr)size,
                IntPtr.Zero,
                NativeMethods.SmtoAbortIfHung,
                100,
                out IntPtr icon);
            if (icon != IntPtr.Zero)
            {
                return icon;
            }
        }

        IntPtr classIcon = NativeMethods.GetClassLongPtr(hwnd, NativeMethods.GclpHicon);
        return classIcon != IntPtr.Zero
            ? classIcon
            : NativeMethods.GetClassLongPtr(hwnd, NativeMethods.GclpHiconSm);
    }

    private static ImageSource ConvertIcon(IntPtr icon)
    {
        ImageSource source = Imaging.CreateBitmapSourceFromHIcon(
            icon,
            Int32Rect.Empty,
            System.Windows.Media.Imaging.BitmapSizeOptions.FromWidthAndHeight(32, 32));
        source.Freeze();
        return source;
    }
}
