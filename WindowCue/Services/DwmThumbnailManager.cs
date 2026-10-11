using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;
using WindowCue.Native;

namespace WindowCue.Services;

internal sealed class DwmThumbnailManager : IDisposable
{
    private sealed record Entry(FrameworkElement Host, IntPtr Thumbnail);

    private readonly IntPtr _destinationWindow;
    private readonly FrameworkElement _viewport;
    private readonly List<Entry> _entries = new();

    internal DwmThumbnailManager(IntPtr destinationWindow, FrameworkElement viewport)
    {
        _destinationWindow = destinationWindow;
        _viewport = viewport;
    }

    internal void Register(FrameworkElement host, IntPtr sourceWindow)
    {
        if (NativeMethods.DwmRegisterThumbnail(
                _destinationWindow,
                sourceWindow,
                out IntPtr thumbnail) == 0 && thumbnail != IntPtr.Zero)
        {
            _entries.Add(new Entry(host, thumbnail));
        }
    }

    internal void UpdateAll()
    {
        if (!_viewport.IsVisible || _viewport.ActualWidth <= 0 || _viewport.ActualHeight <= 0)
        {
            SetAllHidden();
            return;
        }

        NativeMethods.NativePoint clientOrigin = new();
        if (!NativeMethods.ClientToScreen(_destinationWindow, ref clientOrigin))
        {
            return;
        }

        Rect viewportRect;
        try
        {
            Point viewportTopLeft = _viewport.PointToScreen(new Point(0, 0));
            DpiScale viewportDpi = VisualTreeHelper.GetDpi(_viewport);
            viewportRect = new Rect(
                viewportTopLeft.X,
                viewportTopLeft.Y,
                _viewport.ActualWidth * viewportDpi.DpiScaleX,
                _viewport.ActualHeight * viewportDpi.DpiScaleY);
        }
        catch (InvalidOperationException)
        {
            return;
        }

        foreach (Entry entry in _entries)
        {
            Update(entry, clientOrigin, viewportRect);
        }
    }

    internal void Clear()
    {
        foreach (Entry entry in _entries)
        {
            NativeMethods.DwmUnregisterThumbnail(entry.Thumbnail);
        }
        _entries.Clear();
    }

    private static void SetVisible(IntPtr thumbnail, bool visible)
    {
        var properties = new NativeMethods.DwmThumbnailProperties
        {
            Flags = NativeMethods.DwmTnpVisible,
            Visible = visible
        };
        NativeMethods.DwmUpdateThumbnailProperties(thumbnail, ref properties);
    }

    private void SetAllHidden()
    {
        foreach (Entry entry in _entries)
        {
            SetVisible(entry.Thumbnail, false);
        }
    }

    private static void Update(
        Entry entry,
        NativeMethods.NativePoint clientOrigin,
        Rect viewportRect)
    {
        FrameworkElement host = entry.Host;
        if (!host.IsVisible || host.ActualWidth < 2 || host.ActualHeight < 2)
        {
            SetVisible(entry.Thumbnail, false);
            return;
        }

        Point topLeft;
        try
        {
            topLeft = host.PointToScreen(new Point(0, 0));
        }
        catch (InvalidOperationException)
        {
            SetVisible(entry.Thumbnail, false);
            return;
        }

        DpiScale hostDpi = VisualTreeHelper.GetDpi(host);
        var hostRect = new Rect(
            topLeft.X,
            topLeft.Y,
            host.ActualWidth * hostDpi.DpiScaleX,
            host.ActualHeight * hostDpi.DpiScaleY);
        if (!viewportRect.Contains(hostRect.TopLeft) || !viewportRect.Contains(hostRect.BottomRight))
        {
            SetVisible(entry.Thumbnail, false);
            return;
        }

        if (NativeMethods.DwmQueryThumbnailSourceSize(
                entry.Thumbnail,
                out NativeMethods.NativeSize sourceSize) != 0 ||
            sourceSize.Width <= 0 || sourceSize.Height <= 0)
        {
            SetVisible(entry.Thumbnail, false);
            return;
        }

        double scale = Math.Min(hostRect.Width / sourceSize.Width, hostRect.Height / sourceSize.Height);
        int width = Math.Max(1, (int)Math.Round(sourceSize.Width * scale));
        int height = Math.Max(1, (int)Math.Round(sourceSize.Height * scale));
        int left = (int)Math.Round(hostRect.Left - clientOrigin.X + (hostRect.Width - width) / 2.0);
        int top = (int)Math.Round(hostRect.Top - clientOrigin.Y + (hostRect.Height - height) / 2.0);

        var properties = new NativeMethods.DwmThumbnailProperties
        {
            Flags = NativeMethods.DwmTnpRectDestination |
                    NativeMethods.DwmTnpOpacity |
                    NativeMethods.DwmTnpVisible |
                    NativeMethods.DwmTnpSourceClientAreaOnly,
            Destination = new NativeMethods.NativeRect
            {
                Left = left,
                Top = top,
                Right = left + width,
                Bottom = top + height
            },
            Opacity = 255,
            Visible = true,
            SourceClientAreaOnly = false
        };
        NativeMethods.DwmUpdateThumbnailProperties(entry.Thumbnail, ref properties);
    }

    public void Dispose() => Clear();
}
