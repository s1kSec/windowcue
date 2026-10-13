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
        public static string IsolatedStartupRoot(string directory) {
            unchecked {
                ulong hash = 14695981039346656037UL;
                foreach (char ch in directory) {
                    hash ^= char.ToLowerInvariant(ch);
                    hash *= 1099511628211UL;
                }
                return "Software\\WindowCue\\StartupTests\\" + hash.ToString("x16");
            }
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
$secondProcess = $null
$testStartupRoot = [WindowCue.ReleaseStartup]::IsolatedStartupRoot((Resolve-Path -LiteralPath $SettingsDirectory).ProviderPath)
if ($testStartupRoot -notmatch '^Software\\WindowCue\\StartupTests\\[0-9a-f]{16}$') { throw 'Unexpected isolated startup registry path.' }
$existingTestKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testStartupRoot)
if ($existingTestKey) { $existingTestKey.Dispose(); throw 'Startup tests require a new, unused isolated registry subtree.' }
try {
    $env:WINDOWCUE_SETTINGS_DIR = (Resolve-Path -LiteralPath $SettingsDirectory).ProviderPath
    # CreateNoWindow suppresses console creation but does not override a GUI
    # app's first ShowWindow. Start-Process -WindowStyle Hidden would mask an
    # incorrect visible-by-default launch and cannot verify quiet startup.
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $exe
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $hwnd = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        $hwnd = [WindowCue.ReleaseStartup]::Find([uint32]$process.Id, 'WindowCue.Native.Window')
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($hwnd -eq [IntPtr]::Zero -or $process.HasExited) { throw 'The extracted application did not create its normal WindowCue window.' }
    Start-Sleep -Milliseconds 1000
    if ($process.HasExited) { throw 'The extracted application exited during the startup observation period.' }
    if ([WindowCue.ReleaseStartup]::IsWindowVisible($hwnd)) { throw 'The default application launch unexpectedly displayed its main window.' }
    $response = [IntPtr]::Zero
    if ([WindowCue.ReleaseStartup]::SendMessageTimeout($hwnd, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 5000, [ref]$response) -eq [IntPtr]::Zero) {
        throw 'The extracted application window is not responding.'
    }
    $runKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($testStartupRoot + '\Run')
    try {
        if (-not $runKey -or $runKey.GetValue('WindowCue') -cne ('"' + $exe + '" --startup')) {
            throw 'First launch did not register the expected isolated current-user startup command.'
        }
    } finally { if ($runKey) { $runKey.Dispose() } }
    if ((Get-Content -LiteralPath (Join-Path $SettingsDirectory 'startup-preference.txt') -Raw -Encoding UTF8).Trim() -cne 'enabled') {
        throw 'First launch did not persist the enabled startup preference.'
    }
    $secondProcess = [Diagnostics.Process]::Start($startInfo)
    if (-not $secondProcess.WaitForExit(5000) -or $secondProcess.ExitCode -ne 0) {
        throw 'The duplicate launch did not exit successfully.'
    }
    if ($process.HasExited -or [WindowCue.ReleaseStartup]::IsWindowVisible($hwnd)) {
        throw 'Duplicate launch disrupted the existing silent tray instance.'
    }
    [pscustomobject]@{
        Test = 'quiet-startup-and-message-response'; Passed = $true
        WindowCreated = $true; WindowHidden = $true; MessageLoopResponsive = $true
        DefaultStartupRegistered = $true; DuplicateLaunchExited = $true
        LaunchMethod = 'ProcessStartInfo; no GUI ShowWindow override'
        StartupRegistrationScope = ('HKCU\' + $testStartupRoot)
        Cleanup = 'Only newly started test processes and their exact isolated startup registry subtree are removed'
        NormalTrayExit = 'Not verified; automated tray menu interaction was unavailable in the release environment'
    }
} finally {
    # Keep the smoke test bounded; never find or stop an existing app by name.
    if ($secondProcess -and -not $secondProcess.HasExited) { $secondProcess.Kill(); $secondProcess.WaitForExit() }
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKeyTree($testStartupRoot, $false)
    if ($oldForeground -ne [IntPtr]::Zero) { [void][WindowCue.ReleaseStartup]::SetForegroundWindow($oldForeground) }
    [Environment]::SetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', $oldSettings, 'Process')
}
