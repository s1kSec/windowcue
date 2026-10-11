using System;
using System.IO;
using System.Windows.Media;

namespace WindowCue.Models;

internal sealed class WindowDescriptor
{
    internal required IntPtr Handle { get; init; }
    internal required string Title { get; init; }
    internal required string ClassName { get; init; }
    internal required uint ProcessId { get; init; }
    internal required string ProcessPath { get; init; }
    internal ImageSource? Icon { get; init; }

    internal string ProcessName => string.IsNullOrWhiteSpace(ProcessPath)
        ? Strings.UnknownProgram
        : Path.GetFileNameWithoutExtension(ProcessPath);

    internal string Identity => $"{ProcessPath}|{ClassName}|{Title}";
}
