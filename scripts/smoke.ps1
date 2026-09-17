# Development-only Windows GUI smoke test. PowerShell/.NET is not shipped with the app.
param(
    [string]$Exe = "$PSScriptRoot\..\out\build\windows-msvc\bin\Release\POEToolbox.exe",
    [string]$OutputDirectory = "$PSScriptRoot\..\out\validation",
    [switch]$CaptureScreenshots,
    [switch]$LaunchWeb,
    [switch]$VerifyApplicationIcon,
    [string]$CustomTarget = '',
    [ValidateRange(0, 50)][int]$WarmSamples = 20
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class ToolboxSmoke {
    public delegate bool WindowCallback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] static extern bool EnumWindows(WindowCallback callback, IntPtr data);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window, StringBuilder name, int count);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    public static IntPtr FindMainWindow(uint processId) {
        return FindWindow(processId, "POEToolbox.MainWindow");
    }
    public static IntPtr FindWindow(uint processId, string className) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint id; GetWindowThreadProcessId(window, out id);
            if (id == processId) {
                var name = new StringBuilder(128); GetClassName(window, name, name.Capacity);
                if (name.ToString() == className) { result = window; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return result;
    }
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct ScrollInfo { public uint Size, Mask; public int Min, Max; public uint Page; public int Pos, TrackPos; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetSystemMetricsForDpi(int index, uint dpi);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr h);
    [DllImport("user32.dll")] public static extern bool AreDpiAwarenessContextsEqual(IntPtr a, IntPtr b);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr h, string text);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr ReadText(IntPtr h, uint message, IntPtr w, StringBuilder text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr WriteText(IntPtr h, uint message, IntPtr w, string text);
    [DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr h, int bar, ref ScrollInfo info);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
function Assert-True($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Assert-WindowIcons($Window, [uint32]$Dpi) {
    foreach ($kind in @(0,1)) {
        $handle = [ToolboxSmoke]::SendMessage($Window,0x7F,[IntPtr]$kind,[IntPtr]::Zero)
        Assert-True ($handle -ne [IntPtr]::Zero) "Application icon $kind missing at DPI $Dpi"
        $metricX = if ($kind -eq 0) {49} else {11}
        $metricY = if ($kind -eq 0) {50} else {12}
        $expectedWidth = [ToolboxSmoke]::GetSystemMetricsForDpi($metricX,$Dpi)
        $expectedHeight = [ToolboxSmoke]::GetSystemMetricsForDpi($metricY,$Dpi)
        # FromHandle borrows the application's HICON; disposing this wrapper does not destroy it.
        $icon = [Drawing.Icon]::FromHandle($handle)
        try {
            Assert-True ($icon.Width -eq $expectedWidth -and $icon.Height -eq $expectedHeight) "Icon $kind at DPI $Dpi is $($icon.Width)x$($icon.Height), expected ${expectedWidth}x${expectedHeight}"
        } finally { $icon.Dispose() }
    }
}
function Get-Scroll($Window) {
    $info = [ToolboxSmoke+ScrollInfo]::new()
    $info.Size = [Runtime.InteropServices.Marshal]::SizeOf($info)
    $info.Mask = 0x17
    Assert-True ([ToolboxSmoke]::GetScrollInfo($Window, 1, [ref]$info)) 'Cannot read scrollbar'
    return $info
}
function Click-Dip($Window, [double]$X, [double]$Y, [int]$Dpi) {
    $px = [int]($X * $Dpi / 96); $py = [int]($Y * $Dpi / 96)
    [ToolboxSmoke]::SendMessage($Window, 0x201, [IntPtr]::Zero, [IntPtr]($px -bor ($py -shl 16))) | Out-Null
}
function Read-WindowText($Window) {
    $text = [Text.StringBuilder]::new(512)
    [ToolboxSmoke]::ReadText($Window, 0xD, [IntPtr]512, $text) | Out-Null
    return $text.ToString()
}
function Set-Search($Search, [string]$Text) {
    [ToolboxSmoke]::WriteText($Search, 0xC, [IntPtr]::Zero, $Text) | Out-Null
    $actual = [Text.StringBuilder]::new(512)
    [ToolboxSmoke]::ReadText($Search, 0xD, [IntPtr]512, $actual) | Out-Null
    Assert-True ($actual.ToString() -eq $Text) 'Search text did not arrive intact'
}
function Save-Window($Window, [string]$Name) {
    # PrintWindow can omit GPU surfaces on Windows 10/RDP. These images are diagnostic only.
    if (-not $CaptureScreenshots) { return }
    $rect = [ToolboxSmoke+Rect]::new()
    [ToolboxSmoke]::GetWindowRect($Window, [ref]$rect) | Out-Null
    $bitmap = [Drawing.Bitmap]::new($rect.Right - $rect.Left, $rect.Bottom - $rect.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { Assert-True ([ToolboxSmoke]::PrintWindow($Window, $dc, 2)) 'PrintWindow failed' }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    try { $bitmap.Save((Join-Path $OutputDirectory "$Name.png"), [Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
$oldDpi = [ToolboxSmoke]::SetThreadDpiAwarenessContext([IntPtr](-4))
$process = $null
try {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $profile = Join-Path $OutputDirectory 'profile'
    $launch = [Diagnostics.ProcessStartInfo]::new($Exe)
    $launch.UseShellExecute = $false
    $launch.ArgumentList.Add('--data-dir')
    $launch.ArgumentList.Add($profile)
    $process = [Diagnostics.Process]::Start($launch)
    Assert-True ($process.WaitForInputIdle(10000)) 'Message loop did not become idle'
    do {
        $process.Refresh()
        $window = [ToolboxSmoke]::FindMainWindow($process.Id)
        if ($process.HasExited) { throw 'Application exited before first frame' }
        if ($window -ne [IntPtr]::Zero -and [ToolboxSmoke]::GetProp($window, 'POEToolbox.FirstFrame') -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 10
    } while ($timer.ElapsedMilliseconds -lt 10000)
    $startup = $timer.Elapsed.TotalMilliseconds
    Assert-True ($startup -lt 10000) 'First Direct2D frame missing'
    while ([ToolboxSmoke]::GetProp($window, 'POEToolbox.RegistryReady') -eq [IntPtr]::Zero -and $timer.ElapsedMilliseconds -lt 10000) { Start-Sleep -Milliseconds 10 }
    Assert-True ([ToolboxSmoke]::GetProp($window, 'POEToolbox.RegistryReady') -ne [IntPtr]::Zero) 'Registry did not become ready'
    $registryReady = $timer.Elapsed.TotalMilliseconds
    Assert-True ([ToolboxSmoke]::AreDpiAwarenessContextsEqual([ToolboxSmoke]::GetWindowDpiAwarenessContext($window), [IntPtr](-4))) 'Not PerMonitorV2'
    Assert-True (-not [ToolboxSmoke]::IsHungAppWindow($window)) 'Window is unresponsive'
    $actualDpi = [ToolboxSmoke]::GetDpiForWindow($window)
    if ($VerifyApplicationIcon) {
        Assert-WindowIcons $window $actualDpi
    }
    # Stable baseline before search, resize, icon exploration or DPI stress.
    Start-Sleep -Seconds 10
    $process.Refresh()
    $baselineWorking = $process.WorkingSet64
    $baselinePrivate = $process.PrivateMemorySize64
    $baselineCpuStart = $process.TotalProcessorTime.TotalMilliseconds
    $baselineWatch = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 3
    $process.Refresh()
    $baselineCpu = $process.TotalProcessorTime.TotalMilliseconds - $baselineCpuStart
    $baselineWall = $baselineWatch.Elapsed.TotalMilliseconds
    Save-Window $window 'home'
    $search = [ToolboxSmoke]::GetDlgItem($window, 1001)
    $add = [ToolboxSmoke]::GetDlgItem($window, 1002)
    Assert-True ($search -ne [IntPtr]::Zero) 'Search edit control missing'
    Assert-True ($add -eq [IntPtr]::Zero) 'Obsolete toolbar Add button still exists'
    $client = [ToolboxSmoke+Rect]::new()
    [ToolboxSmoke]::GetClientRect($window, [ref]$client) | Out-Null
    Click-Dip $window 70 256 $actualDpi
    Click-Dip $window 410 188 $actualDpi
    Assert-True ((Read-WindowText $window) -eq 'ExileKit — 设置') 'Chinese Settings click did not update window title'
    Click-Dip $window 285 188 $actualDpi
    Assert-True ((Read-WindowText $window) -eq 'ExileKit — Settings') 'English Settings click did not update window title'
    Click-Dip $window 70 124 $actualDpi
    if ($CustomTarget) {
        # Fresh Home's Add Shortcut is a drawn grid item, not a toolbar button.
        $addX = [int](300 * $actualDpi / 96); $addY = [int](280 * $actualDpi / 96)
        [ToolboxSmoke]::PostMessage($window, 0x201, [IntPtr]::Zero, [IntPtr]($addX -bor ($addY -shl 16))) | Out-Null
        $dialogWatch = [Diagnostics.Stopwatch]::StartNew()
        do {
            $dialog = [ToolboxSmoke]::FindWindow($process.Id, '#32770')
            $input = [ToolboxSmoke]::GetDlgItem($dialog, 2101)
            if ($input -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 10
        } while ($dialogWatch.ElapsedMilliseconds -lt 5000)
        Assert-True ($dialog -ne [IntPtr]::Zero) 'Add shortcut dialog did not open'
        $input = [ToolboxSmoke]::GetDlgItem($dialog, 2101)
        Assert-True ($input -ne [IntPtr]::Zero) 'Shortcut input missing'
        Set-Search $input $CustomTarget
        [ToolboxSmoke]::PostMessage($dialog, 0x111, [IntPtr]1, [IntPtr]::Zero) | Out-Null
        $configFile = Join-Path $profile 'Config/settings.json'
        $dialogWatch.Restart()
        do {
            Start-Sleep -Milliseconds 20
            $customConfig = Get-Content -LiteralPath $configFile -Raw | ConvertFrom-Json
            $customEntries = @($customConfig.customTools.PSObject.Properties)
        } while ($customEntries.Count -eq 0 -and $dialogWatch.ElapsedMilliseconds -lt 5000)
        Assert-True ($customEntries.Count -ge 1) 'Dialog submission did not persist shortcut'
        $customId = $customEntries[0].Name
        Click-Dip $window 320 220 $actualDpi
    }
    foreach ($view in @(@(212, 'POE2'), @(168, 'POE'))) {
        Click-Dip $window 70 $view[0] $actualDpi
        Assert-True ((Read-WindowText $window) -eq "ExileKit — $($view[1])") 'Game navigation did not change page'
    }
    # A dense icon grid fits in a large window; use a short viewport to exercise scrolling and filtering.
    [ToolboxSmoke]::SetWindowPos($window, [IntPtr]::Zero, 20, 20, [int](800*$actualDpi/96), [int](520*$actualDpi/96), 0x14) | Out-Null
    $allExtent = (Get-Scroll $window).Max
    Set-Search $search 'poe.ninja'
    Assert-True ((Get-Scroll $window).Max -lt $allExtent) "Search did not reduce real tool layout ($allExtent -> $((Get-Scroll $window).Max))"
    if ($LaunchWeb) {
        Click-Dip $window 300 180 $actualDpi
    }
    Set-Search $search '流放之路 tools'
    Set-Search $search ''
    [ToolboxSmoke]::SendMessage($window, 0x115, [IntPtr]7, [IntPtr]::Zero) | Out-Null
    Assert-True ((Get-Scroll $window).Pos -gt 0) 'Cannot scroll to bottom'
    Save-Window $window 'poe-tools'
    [ToolboxSmoke]::SendMessage($window, 0x115, [IntPtr]6, [IntPtr]::Zero) | Out-Null
    Assert-True ((Get-Scroll $window).Pos -eq 0) 'Cannot scroll to top'
    # A real wheel message, then keyboard Home, exercises their handlers.
    [ToolboxSmoke]::SendMessage($window, 0x20A, [IntPtr](-7864320), [IntPtr]::Zero) | Out-Null
    Assert-True ((Get-Scroll $window).Pos -gt 0) 'Mouse wheel did not scroll'
    [ToolboxSmoke]::SendMessage($window, 0x100, [IntPtr]0x24, [IntPtr]::Zero) | Out-Null
    Assert-True ((Get-Scroll $window).Pos -eq 0) 'Home did not scroll to top'
    [ToolboxSmoke]::SetWindowPos($window, [IntPtr]::Zero, 20, 20, 760, 560, 0x14) | Out-Null
    Save-Window $window 'narrow'
    [ToolboxSmoke]::ShowWindow($window, 6) | Out-Null
    Assert-True ([ToolboxSmoke]::IsIconic($window)) 'Minimize failed'
    [ToolboxSmoke]::ShowWindow($window, 9) | Out-Null
    [ToolboxSmoke]::ShowWindow($window, 3) | Out-Null
    Assert-True ([ToolboxSmoke]::IsZoomed($window)) 'Maximize failed'
    Save-Window $window 'maximized'
    [ToolboxSmoke]::ShowWindow($window, 9) | Out-Null
    # Synthetic WM_DPICHANGED checks handler and font/target relayout. This is not a physical monitor test.
    foreach ($dpi in @(120,144,168,192)) {
        $rect = [ToolboxSmoke+Rect]::new()
        $rect.Left = 20; $rect.Top = 20; $rect.Right = 20 + [int](900 * $dpi/96); $rect.Bottom = 20 + [int](640 * $dpi/96)
        $ptr = [Runtime.InteropServices.Marshal]::AllocHGlobal([Runtime.InteropServices.Marshal]::SizeOf($rect))
        try {
            [Runtime.InteropServices.Marshal]::StructureToPtr($rect, $ptr, $false)
            [ToolboxSmoke]::SendMessage($window, 0x2E0, [IntPtr]($dpi -bor ($dpi -shl 16)), $ptr) | Out-Null
        } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($ptr) }
        Save-Window $window "dpi-$dpi"
        Assert-True (-not [ToolboxSmoke]::IsHungAppWindow($window)) "Unresponsive at DPI $dpi"
        if ($VerifyApplicationIcon) {
            Assert-WindowIcons $window $dpi
        }
    }
    $rect = [ToolboxSmoke+Rect]::new()
    $rect.Left = 20; $rect.Top = 20; $rect.Right = 1220; $rect.Bottom = 820
    $ptr = [Runtime.InteropServices.Marshal]::AllocHGlobal([Runtime.InteropServices.Marshal]::SizeOf($rect))
    try {
        [Runtime.InteropServices.Marshal]::StructureToPtr($rect, $ptr, $false)
        [ToolboxSmoke]::SendMessage($window, 0x2E0, [IntPtr]($actualDpi -bor ($actualDpi -shl 16)), $ptr) | Out-Null
    } finally { [Runtime.InteropServices.Marshal]::FreeHGlobal($ptr) }
    if ($VerifyApplicationIcon) { Assert-WindowIcons $window $actualDpi }
    # Remove focus from edit so the native caret does not affect the idle baseline.
    Click-Dip $window 100 400 $actualDpi
    Start-Sleep -Milliseconds 500
    $process.Refresh()
    $cpuBefore = $process.TotalProcessorTime.TotalMilliseconds
    $idleWatch = [Diagnostics.Stopwatch]::StartNew()
    Start-Sleep -Seconds 3
    $process.Refresh()
    $cpuMs = $process.TotalProcessorTime.TotalMilliseconds - $cpuBefore
    $result = [ordered]@{
        FirstLaunchMs = [math]::Round($startup, 2)
        RegistryReadyMs = [math]::Round($registryReady, 2)
        StartupKind = 'Warm/uncontrolled cache; process start to first-frame marker'
        Dpi = $actualDpi
        BinaryBytes = (Get-Item -LiteralPath $Exe).Length
        BaselineWorkingSetMiB = [math]::Round($baselineWorking/1MB, 2)
        BaselinePrivateMiB = [math]::Round($baselinePrivate/1MB, 2)
        BaselineIdleCpuMs = $baselineCpu
        BaselineIdleSampleMs = [math]::Round($baselineWall, 2)
        LanguageUiSwitch = 'Settings Chinese/English clicks changed window title immediately'
        GameUiFilter = 'Home / POE / POE2 / Settings navigation exercised'
        SearchUi = 'poe.ninja reduced real layout; Unicode edit accepted'
        WorkingSetMB = [math]::Round($process.WorkingSet64/1MB, 2)
        PrivateMB = [math]::Round($process.PrivateMemorySize64/1MB, 2)
        IdleSampleMs = [math]::Round($idleWatch.Elapsed.TotalMilliseconds, 2)
        IdleCpuMs = $cpuMs
        IdleCpuOneCorePercent = [math]::Round($cpuMs / $idleWatch.Elapsed.TotalMilliseconds * 100, 3)
        DpiTests = 'Synthetic messages at 125/150/175/200%; physical multi-monitor untested'
    }
    if ($VerifyApplicationIcon) {
        $result['ApplicationIcon'] = 'Small/large HICON dimensions match system metrics at initial, 125/150/175/200%, and restored DPI'
    }
    Assert-True ([ToolboxSmoke]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)) 'Close request failed'
    Assert-True ($process.WaitForExit(5000)) 'Process survived window close'
    Assert-True ($process.ExitCode -eq 0) 'Nonzero exit code'
    $result['ExitCode'] = $process.ExitCode
    $saved = Get-Content -LiteralPath (Join-Path $profile 'Config/settings.json') -Raw | ConvertFrom-Json
    Assert-True ($saved.language -eq 'en-US' -and $saved.selectedGame -eq 'poe1') 'UI settings did not persist'
    if ($CustomTarget) {
        Assert-True ($saved.recentTools.$customId.launchCount -ge 1) 'Custom card did not launch successfully'
        $result['CustomShortcutUi'] = 'Native Add dialog submitted target; whole-card launch completed and persisted'
    }
    if ($LaunchWeb) {
        Assert-True ($saved.recentTools.'poe-ninja'.launchCount -ge 1) 'Card Open did not complete successfully'
        $result['WebCardOpen'] = 'Actual Open click reached ShellExecuteExW successfully and saved recent; browser page not inspected'
    }
    $samples = @()
    for ($sample = 0; $sample -lt $WarmSamples; $sample++) {
        $process.Dispose()
        $timer.Restart()
        $process = [Diagnostics.Process]::Start($launch)
        do {
            $window = [ToolboxSmoke]::FindMainWindow($process.Id)
            if ($process.HasExited) { throw 'Warm application exited before first frame' }
            if ($window -ne [IntPtr]::Zero -and [ToolboxSmoke]::GetProp($window, 'POEToolbox.FirstFrame') -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 5
        } while ($timer.ElapsedMilliseconds -lt 10000)
        $first = $timer.Elapsed.TotalMilliseconds
        Assert-True ($first -lt 10000) 'Warm first frame timed out'
        while ([ToolboxSmoke]::GetProp($window, 'POEToolbox.RegistryReady') -eq [IntPtr]::Zero -and $timer.ElapsedMilliseconds -lt 10000) { Start-Sleep -Milliseconds 5 }
        Assert-True ([ToolboxSmoke]::GetProp($window, 'POEToolbox.RegistryReady') -ne [IntPtr]::Zero) 'Warm registry timed out'
        $samples += [pscustomobject]@{FirstFrameMs=[math]::Round($first, 2); RegistryReadyMs=[math]::Round($timer.Elapsed.TotalMilliseconds, 2)}
        [ToolboxSmoke]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        Assert-True ($process.WaitForExit(5000) -and $process.ExitCode -eq 0) 'Warm run did not exit cleanly'
    }
    $result['WarmSamples'] = $samples
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'metrics.json') -Encoding utf8
    $result | ConvertTo-Json
} finally {
    if ($process) {
        if (-not $process.HasExited) {
            [ToolboxSmoke]::PostMessage([ToolboxSmoke]::FindMainWindow($process.Id), 0x10, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            if (-not $process.WaitForExit(3000)) { $process.Kill() }
        }
        $process.Dispose()
    }
    [ToolboxSmoke]::SetThreadDpiAwarenessContext($oldDpi) | Out-Null
}
