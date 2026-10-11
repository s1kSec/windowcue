namespace WindowCue.Models;

internal sealed class BindingSettings
{
    public string ProcessPath { get; set; } = string.Empty;
    public string WindowTitle { get; set; } = string.Empty;
    public string ClassName { get; set; } = string.Empty;
    public ulong WindowHandle { get; set; }
    public uint Modifiers { get; set; }
    public uint VirtualKey { get; set; }
    public string HotkeyText { get; set; } = string.Empty;
}
