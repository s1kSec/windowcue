using System;

namespace WindowCue.Models;

internal sealed class ActiveBinding
{
    internal required int HotkeyId { get; init; }
    internal required BindingSettings Settings { get; init; }
    internal required IntPtr TargetHandle { get; init; }
}
