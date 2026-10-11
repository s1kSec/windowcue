namespace WindowCue;

public static class Strings
{
#if LANG_EN
    public static string AppTitle => "WindowCue";
    public static string NoWindows => "No user windows found";
    public static string HotkeyTooltip => "Click and press a shortcut, for example Ctrl + Alt + G";
    public static string Bind => "Bind";
    public static string CancelBinding => "Unbind";
    public static string OpenSettings => "Open settings";
    public static string Exit => "Exit";
    public static string UnknownProgram => "Unknown app";
    public static string ClickToRecord => "Click and press a shortcut";
    public static string UseModifier => "Use Ctrl / Alt / Shift / Win";
    public static string SelectWindowFirst => "Select a window first";
    public static string RecordShortcutFirst => "Record a shortcut with a modifier first";
    public static string ShortcutOccupied(string shortcut) => $"{shortcut} is already used by another app";
#else
    public static string AppTitle => "WindowCue";
    public static string NoWindows => "没有找到用户窗口";
    public static string HotkeyTooltip => "点击后按下快捷键，例如 Ctrl + Alt + G";
    public static string Bind => "绑定";
    public static string CancelBinding => "取消绑定";
    public static string OpenSettings => "打开设置";
    public static string Exit => "退出";
    public static string UnknownProgram => "未知程序";
    public static string ClickToRecord => "点击后按快捷键";
    public static string UseModifier => "请同时使用 Ctrl / Alt / Shift / Win";
    public static string SelectWindowFirst => "请先选择一个窗口";
    public static string RecordShortcutFirst => "请先录入包含修饰键的快捷键";
    public static string ShortcutOccupied(string shortcut) => $"{shortcut} 已被其他程序占用";
#endif
}
