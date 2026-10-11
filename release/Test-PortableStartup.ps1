[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$SettingsDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not ('WindowCue.ReleaseStartup' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace WindowCue {
    public static class ReleaseStartup {
        private delegate bool EnumProc(IntPtr hwnd, IntPtr parameter);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumProc callback, IntPtr parameter);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int capacity);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
        [DllImport("user32.dll", SetLastError = true)] public static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam, uint flags, uint timeout, out IntPtr result);
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
        public static IntPtr Find(uint process, string expectedClass) {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate(IntPtr hwnd, IntPtr unused) {
                uint owner;
                GetWindowThreadProcessId(hwnd, out owner);
                if (owner != process) return true;
                var name = new StringBuilder(256);
                GetClassName(hwnd, name, name.Capacity);
                if (name.ToString() != expectedClass) return true;
                found = hwnd;
                return false;
            }, IntPtr.Zero);
            return found;
        }
    }
}
'@
}

$exe = (Resolve-Path -LiteralPath $Executable).ProviderPath
if (Test-Path -LiteralPath $SettingsDirectory) { throw 'Startup tests require a new, unused settings directory.' }
New-Item -ItemType Directory -Path $SettingsDirectory -Force | Out-Null
# Prevent this fresh test instance from claiming the user's default Alt+P.
[IO.File]::WriteAllText((Join-Path $SettingsDirectory 'self-hotkey-initialized.txt'), "initialized`n", [Text.UTF8Encoding]::new($false))
$oldSettings = [Environment]::GetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', 'Process')
$oldForeground = [WindowCue.ReleaseStartup]::GetForegroundWindow()
$process = $null
try {
    $env:WINDOWCUE_SETTINGS_DIR = (Resolve-Path -LiteralPath $SettingsDirectory).ProviderPath
    $process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $hwnd = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        $hwnd = [WindowCue.ReleaseStartup]::Find([uint32]$process.Id, 'WindowCue.Native.Window')
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($hwnd -eq [IntPtr]::Zero -or $process.HasExited) { throw 'The extracted application did not create its normal WindowCue window.' }
    # Start-Process Hidden intentionally suppresses the first ShowWindow. Verify
    # its normal window/message loop without showing a test window to the user.
    Start-Sleep -Milliseconds 1000
    if ($process.HasExited) { throw 'The extracted application exited during the startup observation period.' }
    $response = [IntPtr]::Zero
    if ([WindowCue.ReleaseStartup]::SendMessageTimeout($hwnd, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$response) -eq [IntPtr]::Zero) {
        throw 'The extracted application window is not responding.'
    }
    [void][WindowCue.ReleaseStartup]::ShowWindow($hwnd, 0)
    [pscustomobject]@{
        Test = 'normal-startup-and-message-response'; Passed = $true
        WindowCreated = $true; MessageLoopResponsive = $true; LaunchWindowStyle = 'Hidden'
        Cleanup = 'Only the newly started test process is terminated after verification'
        NormalTrayExit = 'Not verified; automated tray menu interaction was unavailable in the release environment'
    }
} finally {
    # Keep the smoke test bounded; never find or stop an existing app by name.
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    if ($oldForeground -ne [IntPtr]::Zero) { [void][WindowCue.ReleaseStartup]::SetForegroundWindow($oldForeground) }
    [Environment]::SetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', $oldSettings, 'Process')
}
