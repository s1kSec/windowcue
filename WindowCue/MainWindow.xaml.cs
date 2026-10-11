using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using WindowCue.Controls;
using WindowCue.Models;
using WindowCue.Services;
using Drawing = System.Drawing;
using Forms = System.Windows.Forms;

namespace WindowCue;

public partial class MainWindow : Window
{
    private readonly WindowEnumerator _enumerator = new();
    private readonly DispatcherTimer _thumbnailTimer;
    private readonly DispatcherTimer _foregroundRefreshTimer;
    private readonly Forms.NotifyIcon _trayIcon;
    private readonly Drawing.Icon _trayAppIcon;
    private readonly List<WindowCard> _cards = new();
    private readonly List<ActiveBinding> _bindings = new();
    private DwmThumbnailManager? _thumbnails;
    private HotkeyManager? _hotkeyManager;
    private WindowCard? _selectedCard;
    private WindowDescriptor? _selectedWindow;
    private HotkeyGesture? _capturedGesture;
    private int _nextHotkeyId = 0x5748;
    private string _windowSignature = string.Empty;
    private bool _capturingHotkey;
    private bool _refreshing;
    private bool _allowClose;
    private bool _disposed;

    public MainWindow()
    {
        InitializeComponent();
        _trayAppIcon = LoadApplicationIcon();
        _trayIcon = CreateTrayIcon();

        _thumbnailTimer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(120)
        };
        _thumbnailTimer.Tick += (_, _) => _thumbnails?.UpdateAll();

        _foregroundRefreshTimer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(500)
        };
        _foregroundRefreshTimer.Tick += (_, _) => ForegroundRefreshTick();

        SourceInitialized += MainWindow_OnSourceInitialized;
        Closing += MainWindow_OnClosing;
        Deactivated += (_, _) => EndHotkeyCapture(restoreExisting: true);
        LocationChanged += (_, _) => UpdateViewportHeightLimit();
    }

    private static Drawing.Icon LoadApplicationIcon()
    {
        string? executable = Environment.ProcessPath;
        Drawing.Icon? icon = !string.IsNullOrWhiteSpace(executable)
            ? Drawing.Icon.ExtractAssociatedIcon(executable)
            : null;
        return icon ?? (Drawing.Icon)Drawing.SystemIcons.Application.Clone();
    }

    private Forms.NotifyIcon CreateTrayIcon()
    {
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add(Strings.OpenSettings, null, (_, _) => Dispatcher.Invoke(ShowFromTray));
        menu.Items.Add(new Forms.ToolStripSeparator());
        menu.Items.Add(Strings.Exit, null, (_, _) => Dispatcher.Invoke(ExitApplication));

        var icon = new Forms.NotifyIcon
        {
            Icon = _trayAppIcon,
            Text = Strings.AppTitle,
            ContextMenuStrip = menu,
            Visible = true
        };
        icon.DoubleClick += (_, _) => Dispatcher.Invoke(ShowFromTray);
        return icon;
    }

    private void MainWindow_OnSourceInitialized(object? sender, EventArgs e)
    {
        IntPtr hwnd = new WindowInteropHelper(this).Handle;
        _thumbnails = new DwmThumbnailManager(hwnd, PreviewScroll);
        _hotkeyManager = new HotkeyManager(hwnd);
        _hotkeyManager.Pressed += (_, e) => ToggleBoundWindow(e.HotkeyId);

        UpdateViewportHeightLimit();
        LoadSavedBindings();
        RefreshWindows(force: true);
        _thumbnailTimer.Start();
        _foregroundRefreshTimer.Start();
    }

    private void LoadSavedBindings()
    {
        foreach (BindingSettings settings in SettingsStore.LoadAll())
        {
            if (settings.Modifiers == 0 || settings.VirtualKey == 0)
            {
                continue;
            }

            IntPtr target = _enumerator.FindBestMatch(settings);
            if (target == IntPtr.Zero || _bindings.Any(item => item.TargetHandle == target))
            {
                continue;
            }
            settings.WindowHandle = unchecked((ulong)target.ToInt64());

            int hotkeyId = _nextHotkeyId++;
            var gesture = new HotkeyGesture(
                settings.Modifiers,
                settings.VirtualKey,
                settings.HotkeyText);
            try
            {
                _hotkeyManager!.Register(hotkeyId, gesture);
                _bindings.Add(new ActiveBinding
                {
                    HotkeyId = hotkeyId,
                    Settings = settings,
                    TargetHandle = target
                });
            }
            catch (Win32Exception)
            {
                // Occupied or stale shortcuts are omitted from the migrated set.
            }
        }

        SaveBindings();
    }

    private void ForegroundRefreshTick()
    {
        ValidateBoundWindows();
        if (IsVisible && IsActive)
        {
            RefreshWindows(force: false);
        }
    }

    private void ValidateBoundWindows()
    {
        bool changed = false;
        foreach (ActiveBinding binding in _bindings.ToArray())
        {
            if (binding.TargetHandle == IntPtr.Zero ||
                !Native.NativeMethods.IsWindow(binding.TargetHandle))
            {
                RemoveBinding(binding, save: false);
                changed = true;
            }
        }

        if (changed)
        {
            SaveBindings();
            UpdateBoundBorders();
            RefreshSelectedBindingState();
        }
    }

    private void RefreshWindows(bool force)
    {
        if (_refreshing)
        {
            return;
        }

        _refreshing = true;
        try
        {
            IReadOnlyList<WindowDescriptor> scan = _enumerator.Enumerate(loadIcons: false);
            string signature = CreateWindowSignature(scan);

            if (!force && string.Equals(signature, _windowSignature, StringComparison.Ordinal))
            {
                UpdateBoundBorders();
                return;
            }

            BuildWindowCards(_enumerator.Enumerate(loadIcons: true));
        }
        finally
        {
            _refreshing = false;
        }
    }

    private static string CreateWindowSignature(IReadOnlyList<WindowDescriptor> windows)
    {
        return string.Join(
            "\n",
            windows.Select(window => $"{window.Handle.ToInt64():X}:{window.Title}:{window.ClassName}"));
    }

    private void BuildWindowCards(IReadOnlyList<WindowDescriptor> windows)
    {
        IntPtr selectedHandle = _selectedWindow?.Handle ?? IntPtr.Zero;

        _thumbnails?.Clear();
        WindowPanel.Children.Clear();
        _cards.Clear();
        _selectedCard = null;
        _selectedWindow = null;

        foreach (WindowDescriptor window in windows)
        {
            var card = new WindowCard(window);
            card.SetBound(GetBindingForWindow(window.Handle) is not null);
            card.Selected += (_, _) => SelectCard(card);
            _cards.Add(card);
            WindowPanel.Children.Add(card);
            _thumbnails?.Register(card.PreviewHost, window.Handle);
        }

        _windowSignature = CreateWindowSignature(windows);
        EmptyText.Visibility = windows.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        WindowCard? previous = _cards.FirstOrDefault(card => card.WindowInfo.Handle == selectedHandle);
        if (previous is not null)
        {
            SelectCard(previous, focusHotkey: false);
        }
        else
        {
            ResetSelectionState();
        }

        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, () => _thumbnails?.UpdateAll());
    }

    private void UpdateBoundBorders()
    {
        foreach (WindowCard card in _cards)
        {
            card.SetBound(GetBindingForWindow(card.WindowInfo.Handle) is not null);
        }
    }

    private void SelectCard(WindowCard card, bool focusHotkey = true)
    {
        _selectedCard?.SetSelected(false);
        _selectedCard = card;
        _selectedCard.SetSelected(true);
        _selectedWindow = card.WindowInfo;

        SelectedIcon.Source = card.WindowInfo.Icon;
        SelectedTitle.Text = card.WindowInfo.Title;
        SelectedTitle.Foreground = Brushes.White;
        SelectedProcess.Text = string.IsNullOrWhiteSpace(card.WindowInfo.ProcessPath)
            ? card.WindowInfo.ProcessName
            : card.WindowInfo.ProcessPath;
        ActiveBinding? binding = GetBindingForWindow(card.WindowInfo.Handle);
        if (binding is not null)
        {
            _capturedGesture = new HotkeyGesture(
                binding.Settings.Modifiers,
                binding.Settings.VirtualKey,
                binding.Settings.HotkeyText);
            HotkeyBox.Text = binding.Settings.HotkeyText;
        }
        else
        {
            _capturedGesture = null;
            HotkeyBox.Text = Strings.ClickToRecord;
        }
        UpdateBindingActionState(binding is not null);
        ResetHotkeyBoxBorder();

        if (focusHotkey && binding is null)
        {
            HotkeyBox.Focus();
        }
        _thumbnails?.UpdateAll();
    }

    private void HotkeyBox_OnGotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
    {
        if (!_capturingHotkey)
        {
            _capturingHotkey = true;
            _hotkeyManager?.UnregisterAll();
        }

        if (_capturedGesture is null)
        {
            HotkeyBox.Text = string.Empty;
        }
        HotkeyBox.BorderBrush = new SolidColorBrush(Color.FromRgb(200, 139, 255));
    }

    private void HotkeyBox_OnLostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
    {
        EndHotkeyCapture(restoreExisting: true);
    }

    private void EndHotkeyCapture(bool restoreExisting)
    {
        if (!_capturingHotkey)
        {
            return;
        }

        _capturingHotkey = false;
        ResetHotkeyBoxBorder();
        if (restoreExisting)
        {
            RestoreExistingHotkey();
        }
    }

    private void RestoreExistingHotkey()
    {
        bool changed = false;
        foreach (ActiveBinding binding in _bindings.ToArray())
        {
            if (!Native.NativeMethods.IsWindow(binding.TargetHandle))
            {
                RemoveBinding(binding, save: false);
                changed = true;
                continue;
            }

            try
            {
                _hotkeyManager?.Register(
                    binding.HotkeyId,
                    new HotkeyGesture(
                        binding.Settings.Modifiers,
                        binding.Settings.VirtualKey,
                        binding.Settings.HotkeyText));
            }
            catch (Win32Exception)
            {
                RemoveBinding(binding, save: false);
                changed = true;
            }
        }

        if (changed)
        {
            SaveBindings();
            UpdateBoundBorders();
            RefreshSelectedBindingState();
        }
    }

    private void HotkeyBox_OnPreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        e.Handled = true;
        if (e.Key == Key.Escape)
        {
            _capturedGesture = null;
            HotkeyBox.Text = Strings.ClickToRecord;
            Keyboard.ClearFocus();
            return;
        }

        Key pressedKey = e.Key == Key.System ? e.SystemKey : e.Key;
        if (pressedKey is Key.LeftCtrl or Key.RightCtrl or Key.LeftAlt or Key.RightAlt or
            Key.LeftShift or Key.RightShift or Key.LWin or Key.RWin)
        {
            return;
        }

        if (!HotkeyGesture.TryFromKeyboard(e, out HotkeyGesture gesture))
        {
            HotkeyBox.Text = Strings.UseModifier;
            return;
        }

        _capturedGesture = gesture;
        HotkeyBox.Text = gesture.DisplayText;
    }

    private void BindButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_selectedWindow is null)
        {
            SetInputError(Strings.SelectWindowFirst);
            return;
        }

        ActiveBinding? existing = GetBindingForWindow(_selectedWindow.Handle);
        if (existing is not null)
        {
            RemoveBinding(existing);
            _capturedGesture = null;
            HotkeyBox.Text = Strings.ClickToRecord;
            UpdateBoundBorders();
            UpdateBindingActionState(isBound: false);
            HotkeyBox.Focus();
            return;
        }

        if (_capturedGesture is not HotkeyGesture gesture)
        {
            SetInputError(Strings.RecordShortcutFirst);
            HotkeyBox.Focus();
            return;
        }

        if (_bindings.Any(binding =>
                binding.Settings.Modifiers == gesture.Modifiers &&
                binding.Settings.VirtualKey == gesture.VirtualKey))
        {
            SetInputError(Strings.ShortcutOccupied(gesture.DisplayText));
            return;
        }

        int hotkeyId = _nextHotkeyId++;
        try
        {
            _hotkeyManager!.Register(hotkeyId, gesture);
        }
        catch (Win32Exception)
        {
            SetInputError(Strings.ShortcutOccupied(gesture.DisplayText));
            return;
        }

        var settings = new BindingSettings
        {
            ProcessPath = _selectedWindow.ProcessPath,
            WindowTitle = _selectedWindow.Title,
            ClassName = _selectedWindow.ClassName,
            WindowHandle = unchecked((ulong)_selectedWindow.Handle.ToInt64()),
            Modifiers = gesture.Modifiers,
            VirtualKey = gesture.VirtualKey,
            HotkeyText = gesture.DisplayText
        };
        _bindings.Add(new ActiveBinding
        {
            HotkeyId = hotkeyId,
            Settings = settings,
            TargetHandle = _selectedWindow.Handle
        });
        SaveBindings();
        UpdateBoundBorders();
        EndHotkeyCapture(restoreExisting: false);
        HotkeyBox.Text = gesture.DisplayText;
        UpdateBindingActionState(isBound: true);
    }

    private ActiveBinding? GetBindingForWindow(IntPtr hwnd)
    {
        return _bindings.FirstOrDefault(binding => binding.TargetHandle == hwnd);
    }

    private void RemoveBinding(ActiveBinding binding, bool save = true)
    {
        _hotkeyManager?.Unregister(binding.HotkeyId);
        _bindings.Remove(binding);
        if (save)
        {
            SaveBindings();
        }
    }

    private void SaveBindings()
    {
        SettingsStore.SaveAll(_bindings.Select(binding => binding.Settings));
    }

    private void ToggleBoundWindow(int hotkeyId)
    {
        ActiveBinding? binding = _bindings.FirstOrDefault(item => item.HotkeyId == hotkeyId);
        if (binding is null)
        {
            return;
        }

        if (!Native.NativeMethods.IsWindow(binding.TargetHandle))
        {
            RemoveBinding(binding);
            UpdateBoundBorders();
            RefreshSelectedBindingState();
            return;
        }

        WindowActivator.Toggle(binding.TargetHandle);
    }

    private void RefreshSelectedBindingState()
    {
        if (_selectedWindow is null)
        {
            return;
        }

        ActiveBinding? binding = GetBindingForWindow(_selectedWindow.Handle);
        if (binding is null)
        {
            _capturedGesture = null;
            HotkeyBox.Text = Strings.ClickToRecord;
        }
        else
        {
            _capturedGesture = new HotkeyGesture(
                binding.Settings.Modifiers,
                binding.Settings.VirtualKey,
                binding.Settings.HotkeyText);
            HotkeyBox.Text = binding.Settings.HotkeyText;
        }
        UpdateBindingActionState(binding is not null);
    }

    private void UpdateBindingActionState(bool isBound)
    {
        bool hasSelection = _selectedWindow is not null;
        BindingActionButton.Content = isBound ? Strings.CancelBinding : Strings.Bind;
        BindingActionButton.Background = new SolidColorBrush(
            isBound ? Color.FromRgb(91, 51, 51) : Color.FromRgb(117, 80, 160));
        BindingActionButton.BorderBrush = new SolidColorBrush(
            isBound ? Color.FromRgb(196, 92, 92) : Color.FromRgb(180, 125, 232));
        BindingActionButton.IsEnabled = hasSelection;
        HotkeyBox.IsEnabled = hasSelection && !isBound;
    }

    private void ResetSelectionState()
    {
        _capturedGesture = null;
        SelectedIcon.Source = null;
        SelectedTitle.Text = Strings.SelectWindowFirst;
        SelectedTitle.Foreground = new SolidColorBrush(Color.FromRgb(154, 157, 166));
        SelectedProcess.Text = string.Empty;
        HotkeyBox.Text = string.Empty;
        UpdateBindingActionState(isBound: false);
        ResetHotkeyBoxBorder();
    }

    private void SetInputError(string message)
    {
        HotkeyBox.Text = message;
        HotkeyBox.BorderBrush = Brushes.OrangeRed;
    }

    private void ResetHotkeyBoxBorder()
    {
        HotkeyBox.BorderBrush = new SolidColorBrush(Color.FromRgb(80, 83, 91));
    }

    private void HideToTray()
    {
        EndHotkeyCapture(restoreExisting: true);
        _thumbnails?.UpdateAll();
        Hide();
    }

    private void ShowFromTray()
    {
        Show();
        if (WindowState == WindowState.Minimized)
        {
            WindowState = WindowState.Normal;
        }
        Activate();
        RefreshWindows(force: true);
    }

    private void MinimizeButton_OnClick(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }

    private void MaximizeButton_OnClick(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState == WindowState.Maximized
            ? WindowState.Normal
            : WindowState.Maximized;
    }

    private void CloseButton_OnClick(object sender, RoutedEventArgs e)
    {
        HideToTray();
    }

    private void MainWindow_OnStateChanged(object? sender, EventArgs e)
    {
        if (WindowState == WindowState.Minimized)
        {
            HideToTray();
        }
    }

    private void MainWindow_OnClosing(object? sender, CancelEventArgs e)
    {
        if (_allowClose)
        {
            return;
        }
        e.Cancel = true;
        HideToTray();
    }

    private void QueueViewportHeightUpdate()
    {
        Dispatcher.BeginInvoke(DispatcherPriority.Loaded, UpdateViewportHeightLimit);
    }

    private void UpdateViewportHeightLimit()
    {
        IntPtr hwnd = new WindowInteropHelper(this).Handle;
        if (hwnd == IntPtr.Zero)
        {
            return;
        }

        Forms.Screen screen = Forms.Screen.FromHandle(hwnd);
        PresentationSource? source = PresentationSource.FromVisual(this);
        double scaleY = source?.CompositionTarget?.TransformToDevice.M22 ?? 1d;
        double workingHeight = screen.WorkingArea.Height / Math.Max(scaleY, 0.1d);

        MaxHeight = Math.Max(320d, workingHeight);
        double bindingHeight = Math.Max(BindingPanel.ActualHeight, BindingPanel.MinHeight) + 12d;
        const double fixedChromeAndMargins = 34d + 34d;
        PreviewScroll.MaxHeight = Math.Max(
            PreviewScroll.MinHeight,
            MaxHeight - fixedChromeAndMargins - bindingHeight);
    }

    private void ExitApplication()
    {
        if (_disposed)
        {
            return;
        }
        _allowClose = true;
        _disposed = true;
        _thumbnailTimer.Stop();
        _foregroundRefreshTimer.Stop();
        _thumbnails?.Dispose();
        _hotkeyManager?.Dispose();
        _trayIcon.Visible = false;
        _trayIcon.Dispose();
        _trayAppIcon.Dispose();
        Close();
        System.Windows.Application.Current.Shutdown();
    }

    protected override void OnClosed(EventArgs e)
    {
        if (!_disposed)
        {
            _disposed = true;
            _thumbnailTimer.Stop();
            _foregroundRefreshTimer.Stop();
            _thumbnails?.Dispose();
            _hotkeyManager?.Dispose();
            _trayIcon.Visible = false;
            _trayIcon.Dispose();
            _trayAppIcon.Dispose();
        }
        base.OnClosed(e);
    }
}
