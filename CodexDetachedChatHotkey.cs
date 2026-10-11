using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Automation;
using System.Windows.Forms;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length > 0 && string.Equals(args[0], "--self-test", StringComparison.OrdinalIgnoreCase))
        {
            return SelfTest.Run();
        }

        if (args.Length > 0 && string.Equals(args[0], "--ui-self-test", StringComparison.OrdinalIgnoreCase))
        {
            return UiAutomationSelfTest.Run();
        }

        if (args.Length > 0 && string.Equals(args[0], "--probe", StringComparison.OrdinalIgnoreCase))
        {
            IntPtr hwnd = WindowDiscovery.FindDetachedQuickChatWindow();
            Log.Write("Probe result HWND=" + hwnd + ".");
            return hwnd == IntPtr.Zero ? 2 : 0;
        }

        if (args.Length > 0 && string.Equals(args[0], "--focus-self-test", StringComparison.OrdinalIgnoreCase))
        {
            return FocusSelfTest.Run();
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        try
        {
            Application.Run(new HotkeyApplicationContext());
            return 0;
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                ex.Message,
                "Codex 独立聊天快捷键",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }
}

internal sealed class HotkeyApplicationContext : ApplicationContext
{
    private readonly NotifyIcon _trayIcon;
    private readonly HotkeyMessageWindow _messageWindow;
    private IntPtr _detachedWindow = IntPtr.Zero;
    private int _isWorking;

    internal HotkeyApplicationContext()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("显示/最小化独立聊天 (Alt+X)", null, delegate { BeginOpenOrActivate(); });
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("退出", null, delegate { ExitThread(); });

        _trayIcon = new NotifyIcon();
        _trayIcon.Icon = SystemIcons.Application;
        _trayIcon.Text = "Codex 独立聊天快捷键：Alt+X";
        _trayIcon.ContextMenuStrip = menu;
        _trayIcon.Visible = true;
        _trayIcon.DoubleClick += delegate { BeginOpenOrActivate(); };

        _messageWindow = new HotkeyMessageWindow(BeginOpenOrActivate);
        if (!_messageWindow.Register())
        {
            _trayIcon.Visible = false;
            _trayIcon.Dispose();
            _messageWindow.Dispose();
            throw new InvalidOperationException(
                "无法注册 Alt+X。请先退出旧版托盘程序，并确认没有其他程序占用 Alt+X。");
        }

        Log.Write("Started and registered Alt+X.");
        ShowNotice("Alt+X 已启用", "按 Alt+X 可显示或最小化 Quick Chat 独立窗口。", ToolTipIcon.Info);
    }

    private void BeginOpenOrActivate()
    {
        if (Interlocked.Exchange(ref _isWorking, 1) != 0)
        {
            return;
        }

        ThreadPool.QueueUserWorkItem(delegate
        {
            try
            {
                OpenOrActivate();
            }
            catch (Exception ex)
            {
                Log.Write("Operation failed: " + ex);
                // Operational failures stay silent so no long-lived Windows notification
                // interrupts the user. Full details remain available in the log.
            }
            finally
            {
                Interlocked.Exchange(ref _isWorking, 0);
            }
        });
    }

    private void OpenOrActivate()
    {
        if (_detachedWindow != IntPtr.Zero && NativeMethods.IsWindow(_detachedWindow))
        {
            WindowDiscovery.Toggle(_detachedWindow);
            Log.Write("Toggled tracked detached window " + _detachedWindow + ".");
            return;
        }

        _detachedWindow = IntPtr.Zero;

        IntPtr discoveredWindow = WindowDiscovery.FindDetachedQuickChatWindow();
        if (discoveredWindow != IntPtr.Zero)
        {
            _detachedWindow = discoveredWindow;
            WindowDiscovery.Toggle(discoveredWindow);
            Log.Write("Discovered and toggled detached window " + discoveredWindow + ".");
            return;
        }

        HashSet<IntPtr> windowsBefore = WindowDiscovery.GetVisibleChatGptWindows();

        NativeMethods.WaitForAltAndXRelease();
        if (!NativeMethods.SendQuickChatShortcut())
        {
            throw new InvalidOperationException(
                "发送 Ctrl+Alt+N 失败。Win32 错误码：" + Marshal.GetLastWin32Error() + "。");
        }

        Log.Write("Sent Ctrl+Alt+N; searching for the Open in new window button.");
        IntPtr quickChatHost;
        if (!QuickChatAutomation.WaitAndInvokeOpenInNewWindow(out quickChatHost, TimeSpan.FromSeconds(6)))
        {
            throw new InvalidOperationException(
                "已发送 Ctrl+Alt+N，但没有找到“在新窗口中打开”按钮。请确认 Quick Chat 能正常出现，并保持 Codex 界面语言为中文或英文。");
        }

        IntPtr created = WindowDiscovery.WaitForNewChatGptWindow(windowsBefore, TimeSpan.FromSeconds(8));
        if (created == IntPtr.Zero)
        {
            throw new InvalidOperationException(
                "已触发“在新窗口中打开”，但没有检测到新的独立窗口。请检查 Codex 是否把窗口创建在其他桌面或显示器上。");
        }

        _detachedWindow = created;
        WindowDiscovery.Activate(created);
        Log.Write("Created and tracked detached window " + created + ".");
    }

    private void ShowNotice(string title, string text, ToolTipIcon icon)
    {
        try
        {
            _trayIcon.ShowBalloonTip(3500, title, text, icon);
        }
        catch
        {
            // A notification failure must not break the hotkey.
        }
    }

    protected override void ExitThreadCore()
    {
        Log.Write("Exiting.");
        _messageWindow.Dispose();
        _trayIcon.Visible = false;
        _trayIcon.Dispose();
        base.ExitThreadCore();
    }
}

internal sealed class HotkeyMessageWindow : NativeWindow, IDisposable
{
    private const int HotkeyId = 0x4348;
    private readonly Action _onHotkey;
    private bool _registered;

    internal HotkeyMessageWindow(Action onHotkey)
    {
        _onHotkey = onHotkey;
        CreateHandle(new CreateParams());
    }

    internal bool Register()
    {
        _registered = NativeMethods.RegisterHotKey(
            Handle,
            HotkeyId,
            NativeMethods.ModAlt | NativeMethods.ModNoRepeat,
            NativeMethods.VirtualKeyX);
        return _registered;
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == NativeMethods.WmHotkey && m.WParam.ToInt32() == HotkeyId)
        {
            _onHotkey();
        }
        base.WndProc(ref m);
    }

    public void Dispose()
    {
        if (_registered)
        {
            NativeMethods.UnregisterHotKey(Handle, HotkeyId);
            _registered = false;
        }
        DestroyHandle();
    }
}

internal static class QuickChatAutomation
{
    internal static bool WaitAndInvokeOpenInNewWindow(out IntPtr hostWindow, TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow.Add(timeout);
        hostWindow = IntPtr.Zero;

        while (DateTime.UtcNow < deadline)
        {
            var candidates = new List<IntPtr>();
            IntPtr foreground = NativeMethods.GetForegroundWindow();
            if (foreground != IntPtr.Zero && WindowDiscovery.IsChatGptWindow(foreground))
            {
                candidates.Add(foreground);
            }

            foreach (IntPtr hwnd in WindowDiscovery.GetVisibleChatGptWindows())
            {
                if (!candidates.Contains(hwnd))
                {
                    candidates.Add(hwnd);
                }
            }

            foreach (IntPtr hwnd in candidates)
            {
                if (TryInvokeButton(hwnd))
                {
                    hostWindow = hwnd;
                    Log.Write("Invoked Open in new window via UI Automation in " + hwnd + ".");
                    return true;
                }
            }

            Thread.Sleep(120);
        }

        return false;
    }

    internal static bool TryInvokeButton(IntPtr hwnd)
    {
        try
        {
            AutomationElement root = AutomationElement.FromHandle(hwnd);
            if (root == null)
            {
                return false;
            }

            AutomationElementCollection elements = root.FindAll(TreeScope.Descendants, Condition.TrueCondition);
            for (int i = 0; i < elements.Count; i++)
            {
                AutomationElement element = elements[i];
                string name;
                try
                {
                    name = element.Current.Name;
                }
                catch (ElementNotAvailableException)
                {
                    continue;
                }

                if (!OpenInNewWindowLabel.IsMatch(name))
                {
                    continue;
                }

                object pattern;
                if (element.TryGetCurrentPattern(InvokePattern.Pattern, out pattern))
                {
                    ((InvokePattern)pattern).Invoke();
                    return true;
                }

            }
        }
        catch (Exception ex)
        {
            Log.Write("UI Automation probe failed for " + hwnd + ": " + ex.GetType().Name + ".");
        }

        return false;
    }
}

internal static class OpenInNewWindowLabel
{
    private static readonly string[] Labels =
    {
        "open in new window",
        "open in a new window",
        "在新窗口中打开",
        "在新視窗中開啟",
        "在新視窗中打開",
        "新しいウィンドウで開く",
        "새 창에서 열기"
    };

    internal static bool IsMatch(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        string normalized = value.Trim().ToLowerInvariant();
        for (int i = 0; i < Labels.Length; i++)
        {
            if (normalized.IndexOf(Labels[i], StringComparison.Ordinal) >= 0)
            {
                return true;
            }
        }
        return false;
    }
}

internal static class WindowDiscovery
{
    private const int DetachedWindowScoreThreshold = 190;

    internal static HashSet<IntPtr> GetVisibleChatGptWindows()
    {
        var result = new HashSet<IntPtr>();
        NativeMethods.EnumWindows(delegate(IntPtr hwnd, IntPtr ignored)
        {
            if (NativeMethods.IsWindowVisible(hwnd) && IsChatGptWindow(hwnd))
            {
                result.Add(hwnd);
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }

    internal static IntPtr FindDetachedQuickChatWindow()
    {
        IntPtr bestWindow = IntPtr.Zero;
        int bestScore = int.MinValue;

        NativeMethods.EnumWindows(delegate(IntPtr hwnd, IntPtr ignored)
        {
            if ((!NativeMethods.IsWindowVisible(hwnd) && !NativeMethods.IsIconic(hwnd)) || !IsChatGptWindow(hwnd))
            {
                return true;
            }

            string title = NativeMethods.GetTitle(hwnd);
            string className = NativeMethods.GetClassNameText(hwnd);
            NativeMethods.WindowRect rect;
            if (!NativeMethods.GetWindowRect(hwnd, out rect))
            {
                return true;
            }

            uint dpi = NativeMethods.GetWindowDpi(hwnd);
            int width = Math.Max(0, rect.Right - rect.Left);
            int height = Math.Max(0, rect.Bottom - rect.Top);
            int score = ScoreDetachedWindow(title, className, width, height, dpi);

            Log.Write(string.Format(
                "HWND candidate={0}, title={1}, class={2}, size={3}x{4}, dpi={5}, score={6}.",
                hwnd,
                string.IsNullOrEmpty(title) ? "<empty>" : title,
                className,
                width,
                height,
                dpi,
                score));

            if (score > bestScore)
            {
                bestScore = score;
                bestWindow = hwnd;
            }
            return true;
        }, IntPtr.Zero);

        return bestScore >= DetachedWindowScoreThreshold ? bestWindow : IntPtr.Zero;
    }

    internal static int ScoreDetachedWindow(string title, string className, int width, int height, uint dpi)
    {
        int score = 0;
        if (string.Equals(title, "ChatGPT", StringComparison.OrdinalIgnoreCase))
        {
            score += 140;
        }

        if (className.StartsWith("Chrome_WidgetWin_", StringComparison.OrdinalIgnoreCase))
        {
            score += 20;
        }

        double scale = dpi > 0 ? dpi / 96.0 : 1.0;
        double expectedWidth = 560.0 * scale;
        double expectedHeight = 700.0 * scale;
        double widthError = Math.Abs(width - expectedWidth) / expectedWidth;
        double heightError = Math.Abs(height - expectedHeight) / expectedHeight;
        double averageError = (widthError + heightError) / 2.0;

        if (widthError <= 0.45 && heightError <= 0.45)
        {
            score += Math.Max(0, (int)Math.Round(80.0 * (1.0 - averageError)));
        }

        if (height > 0)
        {
            double aspectError = Math.Abs((width / (double)height) - 0.8);
            if (aspectError <= 0.20)
            {
                score += 15;
            }
        }

        return score;
    }

    internal static bool IsChatGptWindow(IntPtr hwnd)
    {
        uint processId;
        NativeMethods.GetWindowThreadProcessId(hwnd, out processId);
        if (processId == 0)
        {
            return false;
        }

        try
        {
            using (Process process = Process.GetProcessById((int)processId))
            {
                return string.Equals(process.ProcessName, "ChatGPT", StringComparison.OrdinalIgnoreCase);
            }
        }
        catch
        {
            return false;
        }
    }

    internal static IntPtr WaitForNewChatGptWindow(HashSet<IntPtr> previous, TimeSpan timeout)
    {
        DateTime deadline = DateTime.UtcNow.Add(timeout);
        while (DateTime.UtcNow < deadline)
        {
            foreach (IntPtr hwnd in GetVisibleChatGptWindows())
            {
                if (!previous.Contains(hwnd))
                {
                    return hwnd;
                }
            }
            Thread.Sleep(120);
        }
        return IntPtr.Zero;
    }

    internal static void Activate(IntPtr hwnd)
    {
        if (NativeMethods.IsIconic(hwnd))
        {
            NativeMethods.ShowWindowAsync(hwnd, NativeMethods.SwRestore);
        }
        else
        {
            NativeMethods.ShowWindowAsync(hwnd, NativeMethods.SwShow);
        }

        Thread.Sleep(40);
        NativeMethods.SetWindowPos(
            hwnd,
            NativeMethods.HwndTop,
            0,
            0,
            0,
            0,
            NativeMethods.SwpNoMove | NativeMethods.SwpNoSize | NativeMethods.SwpShowWindow);
        NativeMethods.BringWindowToTop(hwnd);

        bool foregroundSet = NativeMethods.SetForegroundWindow(hwnd);
        if (!foregroundSet || NativeMethods.GetForegroundWindow() != hwnd)
        {
            uint targetProcessId;
            uint targetThread = NativeMethods.GetWindowThreadProcessId(hwnd, out targetProcessId);
            uint currentThread = NativeMethods.GetCurrentThreadId();
            IntPtr foregroundWindow = NativeMethods.GetForegroundWindow();
            uint foregroundProcessId;
            uint foregroundThread = foregroundWindow == IntPtr.Zero
                ? 0
                : NativeMethods.GetWindowThreadProcessId(foregroundWindow, out foregroundProcessId);

            bool attachedTarget = currentThread != targetThread &&
                                  NativeMethods.AttachThreadInput(currentThread, targetThread, true);
            bool attachedForeground = foregroundThread != 0 &&
                                      foregroundThread != currentThread &&
                                      foregroundThread != targetThread &&
                                      NativeMethods.AttachThreadInput(currentThread, foregroundThread, true);
            try
            {
                NativeMethods.BringWindowToTop(hwnd);
                foregroundSet = NativeMethods.SetForegroundWindow(hwnd);
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

        Log.Write("Activate HWND=" + hwnd + ", foregroundSet=" + foregroundSet +
                  ", actualForeground=" + NativeMethods.GetForegroundWindow() + ".");
    }

    internal static void Toggle(IntPtr hwnd)
    {
        if (NativeMethods.GetForegroundWindow() == hwnd && !NativeMethods.IsIconic(hwnd))
        {
            NativeMethods.ShowWindowAsync(hwnd, NativeMethods.SwMinimize);
            Log.Write("Minimized foreground HWND=" + hwnd + ".");
            return;
        }

        Activate(hwnd);
    }
}

internal static class NativeMethods
{
    internal const int WmHotkey = 0x0312;
    internal const uint ModAlt = 0x0001;
    internal const uint ModNoRepeat = 0x4000;
    internal const uint VirtualKeyX = 0x58;
    internal const ushort VkControl = 0x11;
    internal const ushort VkMenu = 0x12;
    internal const ushort VkN = 0x4E;
    internal const ushort VkX = 0x58;
    internal const uint InputKeyboard = 1;
    internal const uint KeyEventKeyUp = 0x0002;
    internal const int SwShow = 5;
    internal const int SwMinimize = 6;
    internal const int SwRestore = 9;
    internal const uint SwpNoSize = 0x0001;
    internal const uint SwpNoMove = 0x0002;
    internal const uint SwpShowWindow = 0x0040;
    internal static readonly IntPtr HwndTop = IntPtr.Zero;

    internal delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool RegisterHotKey(IntPtr hwnd, int id, uint modifiers, uint virtualKey);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool UnregisterHotKey(IntPtr hwnd, int id);

    [DllImport("user32.dll")]
    internal static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

    [DllImport("user32.dll")]
    internal static extern bool IsWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    internal static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    internal static extern bool IsIconic(IntPtr hwnd);

    [DllImport("user32.dll")]
    internal static extern bool ShowWindowAsync(IntPtr hwnd, int command);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool SetWindowPos(
        IntPtr hwnd,
        IntPtr insertAfter,
        int x,
        int y,
        int width,
        int height,
        uint flags);

    [DllImport("user32.dll")]
    internal static extern bool BringWindowToTop(IntPtr hwnd);

    [DllImport("user32.dll")]
    internal static extern bool SetForegroundWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    internal static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    internal static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);

    [DllImport("kernel32.dll")]
    internal static extern uint GetCurrentThreadId();

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool attach);

    [DllImport("user32.dll", SetLastError = true)]
    internal static extern bool GetWindowRect(IntPtr hwnd, out WindowRect rect);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int maxCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern int GetClassName(IntPtr hwnd, StringBuilder className, int maxCount);

    [DllImport("user32.dll", EntryPoint = "GetDpiForWindow")]
    private static extern uint GetDpiForWindowNative(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern short GetAsyncKeyState(int virtualKey);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint SendInput(uint count, Input[] inputs, int size);

    internal static void WaitForAltAndXRelease()
    {
        DateTime deadline = DateTime.UtcNow.AddMilliseconds(800);
        while (DateTime.UtcNow < deadline)
        {
            bool altDown = (GetAsyncKeyState(VkMenu) & 0x8000) != 0;
            bool xDown = (GetAsyncKeyState(VkX) & 0x8000) != 0;
            if (!altDown && !xDown)
            {
                return;
            }
            Thread.Sleep(10);
        }
    }

    internal static bool SendQuickChatShortcut()
    {
        Input[] inputs =
        {
            Key(VkControl, false),
            Key(VkMenu, false),
            Key(VkN, false),
            Key(VkN, true),
            Key(VkMenu, true),
            Key(VkControl, true)
        };
        int inputSize = Marshal.SizeOf(typeof(Input));
        uint sent = SendInput((uint)inputs.Length, inputs, inputSize);
        Log.Write("SendInput sent=" + sent + "/" + inputs.Length + ", cbSize=" + inputSize +
                  ", error=" + Marshal.GetLastWin32Error() + ".");
        return sent == (uint)inputs.Length;
    }

    internal static string GetTitle(IntPtr hwnd)
    {
        var text = new StringBuilder(512);
        GetWindowText(hwnd, text, text.Capacity);
        return text.ToString();
    }

    internal static string GetClassNameText(IntPtr hwnd)
    {
        var text = new StringBuilder(256);
        GetClassName(hwnd, text, text.Capacity);
        return text.ToString();
    }

    internal static uint GetWindowDpi(IntPtr hwnd)
    {
        try
        {
            uint dpi = GetDpiForWindowNative(hwnd);
            return dpi == 0 ? 96U : dpi;
        }
        catch (EntryPointNotFoundException)
        {
            return 96U;
        }
    }

    private static Input Key(ushort virtualKey, bool keyUp)
    {
        var input = new Input();
        input.Type = InputKeyboard;
        input.Union.Keyboard.VirtualKey = virtualKey;
        input.Union.Keyboard.Flags = keyUp ? KeyEventKeyUp : 0;
        return input;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Input
    {
        internal uint Type;
        internal InputUnion Union;
    }

    [StructLayout(LayoutKind.Explicit)]
    internal struct InputUnion
    {
        [FieldOffset(0)]
        internal MouseInput Mouse;

        [FieldOffset(0)]
        internal KeyboardInput Keyboard;

        [FieldOffset(0)]
        internal HardwareInput Hardware;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct MouseInput
    {
        internal int Dx;
        internal int Dy;
        internal uint MouseData;
        internal uint Flags;
        internal uint Time;
        internal UIntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct KeyboardInput
    {
        internal ushort VirtualKey;
        internal ushort ScanCode;
        internal uint Flags;
        internal uint Time;
        internal UIntPtr ExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct HardwareInput
    {
        internal uint Message;
        internal ushort ParamL;
        internal ushort ParamH;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct WindowRect
    {
        internal int Left;
        internal int Top;
        internal int Right;
        internal int Bottom;
    }
}

internal static class Log
{
    private static readonly object Sync = new object();
    private static readonly string DirectoryPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "CodexDetachedChatHotkey");
    private static readonly string FilePath = Path.Combine(DirectoryPath, "hotkey.log");

    internal static void Write(string message)
    {
        try
        {
            lock (Sync)
            {
                Directory.CreateDirectory(DirectoryPath);
                File.AppendAllText(FilePath, DateTime.Now.ToString("s") + " " + message + Environment.NewLine);
            }
        }
        catch
        {
            // Logging is best-effort only.
        }
    }
}

internal static class SelfTest
{
    internal static int Run()
    {
        bool passed = true;
        passed &= OpenInNewWindowLabel.IsMatch("在新窗口中打开");
        passed &= OpenInNewWindowLabel.IsMatch("Open in new window");
        passed &= OpenInNewWindowLabel.IsMatch("Open in a new window");
        passed &= !OpenInNewWindowLabel.IsMatch("打开设置");
        passed &= !OpenInNewWindowLabel.IsMatch(null);
        int expectedInputSize = IntPtr.Size == 8 ? 40 : 28;
        passed &= Marshal.SizeOf(typeof(NativeMethods.Input)) == expectedInputSize;
        passed &= WindowDiscovery.ScoreDetachedWindow(
            "ChatGPT", "Chrome_WidgetWin_1", 700, 875, 120) >= 190;
        passed &= WindowDiscovery.ScoreDetachedWindow(
            "ChatGPT", "Chrome_WidgetWin_1", 1920, 1080, 120) < 190;

        Console.WriteLine(passed ? "SELF-TEST PASS" : "SELF-TEST FAIL");
        return passed ? 0 : 1;
    }
}

internal static class UiAutomationSelfTest
{
    internal static int Run()
    {
        int result = 1;
        using (var form = new Form())
        using (var button = new Button())
        {
            form.Text = "CodexDetachedChatHotkey UI test";
            form.ShowInTaskbar = false;
            form.StartPosition = FormStartPosition.Manual;
            form.Location = new Point(40, 40);
            form.Size = new Size(320, 120);

            button.Text = "在新窗口中打开";
            button.Dock = DockStyle.Fill;
            button.Click += delegate
            {
                result = 0;
                form.Close();
            };
            form.Controls.Add(button);

            form.Shown += delegate
            {
                ThreadPool.QueueUserWorkItem(delegate
                {
                    Thread.Sleep(400);
                    bool invoked = QuickChatAutomation.TryInvokeButton(form.Handle);
                    if (!invoked)
                    {
                        form.BeginInvoke((Action)delegate { form.Close(); });
                    }
                });
            };

            Application.Run(form);
        }

        return result;
    }
}

internal static class FocusSelfTest
{
    internal static int Run()
    {
        int result = 1;
        using (var form = new Form())
        {
            form.Text = "CodexDetachedChatHotkey focus test";
            form.ShowInTaskbar = false;
            form.StartPosition = FormStartPosition.Manual;
            form.Location = new Point(80, 80);
            form.Size = new Size(360, 180);

            form.Shown += delegate
            {
                IntPtr hwnd = form.Handle;
                form.WindowState = FormWindowState.Minimized;
                ThreadPool.QueueUserWorkItem(delegate
                {
                    Thread.Sleep(300);
                    WindowDiscovery.Activate(hwnd);
                    Thread.Sleep(150);
                    bool initiallyRestored = !NativeMethods.IsIconic(hwnd);
                    bool initiallyFocused = NativeMethods.GetForegroundWindow() == hwnd;

                    WindowDiscovery.Toggle(hwnd);
                    Thread.Sleep(150);
                    bool minimized = NativeMethods.IsIconic(hwnd);

                    WindowDiscovery.Toggle(hwnd);
                    Thread.Sleep(150);
                    bool restoredAgain = !NativeMethods.IsIconic(hwnd);
                    bool focusedAgain = NativeMethods.GetForegroundWindow() == hwnd;

                    result = initiallyRestored && initiallyFocused && minimized &&
                             restoredAgain && focusedAgain ? 0 : 1;
                    form.BeginInvoke((Action)delegate { form.Close(); });
                });
            };

            Application.Run(form);
        }

        return result;
    }
}
