# Development-only comparison across versions; no navigation, HTTP requests or tool launches.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutputDirectory,
      [ValidateRange(1,50)][int]$WarmSamples = 20)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ExileKitStartupProbe {
    delegate bool Callback(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumWindows(Callback c, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder b, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string s);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    public static IntPtr Find(uint pid) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((h,p)=>{uint id; GetWindowThreadProcessId(h,out id);
            if(id==pid) {var b=new StringBuilder(128); GetClassName(h,b,128);
                if(b.ToString()=="POEToolbox.MainWindow") {found=h; return false;}}
            return true;},IntPtr.Zero);
        return found;
    }
}
'@
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$info = [Diagnostics.ProcessStartInfo]::new($Exe)
$info.UseShellExecute = $false
$info.ArgumentList.Add('--data-dir')
$info.ArgumentList.Add((Join-Path $OutputDirectory 'profile'))
$samples = @()
$process = $null
try {
    for ($run=0; $run -le $WarmSamples; ++$run) {
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $process = [Diagnostics.Process]::Start($info)
        $window = [IntPtr]::Zero
        do {
            if ($process.HasExited) { throw 'Application exited before first frame.' }
            $window = [ExileKitStartupProbe]::Find($process.Id)
            if ($window -ne [IntPtr]::Zero -and [ExileKitStartupProbe]::GetProp($window,'POEToolbox.FirstFrame') -ne [IntPtr]::Zero) { break }
            Start-Sleep -Milliseconds 5
        } while ($watch.ElapsedMilliseconds -lt 10000)
        if ($watch.ElapsedMilliseconds -ge 10000) { throw 'First frame timeout.' }
        $first = [math]::Round($watch.Elapsed.TotalMilliseconds,2)
        while ([ExileKitStartupProbe]::GetProp($window,'POEToolbox.RegistryReady') -eq [IntPtr]::Zero -and $watch.ElapsedMilliseconds -lt 10000) { Start-Sleep -Milliseconds 5 }
        if ($watch.ElapsedMilliseconds -ge 10000) { throw 'Registry readiness timeout.' }
        $ready = [math]::Round($watch.Elapsed.TotalMilliseconds,2)
        if ($run -eq 0) {
            Start-Sleep -Seconds 10
            $process.Refresh()
            $working = $process.WorkingSet64
            $privateBytes = $process.PrivateMemorySize64
            $cpu = $process.TotalProcessorTime.TotalMilliseconds
            $idleWatch = [Diagnostics.Stopwatch]::StartNew()
            Start-Sleep -Seconds 3
            $process.Refresh()
            $result = [ordered]@{BinaryBytes=(Get-Item -LiteralPath $Exe).Length;FirstRunMs=$first;RegistryReadyMs=$ready;
                WorkingSetMiB=[math]::Round($working/1MB,2);PrivateMiB=[math]::Round($privateBytes/1MB,2);
                IdleCpuMs=$process.TotalProcessorTime.TotalMilliseconds-$cpu;IdleWallMs=[math]::Round($idleWatch.Elapsed.TotalMilliseconds,2)}
        } else { $samples += [pscustomobject]@{FirstFrameMs=$first;RegistryReadyMs=$ready} }
        [ExileKitStartupProbe]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        if (-not $process.WaitForExit(5000) -or $process.ExitCode -ne 0) { throw 'Application did not close cleanly.' }
        $process.Dispose(); $process=$null
    }
    $sorted = @($samples.FirstFrameMs | Sort-Object)
    $middle = [int][math]::Floor($sorted.Count/2)
    $median = if ($sorted.Count % 2) {$sorted[$middle]} else {($sorted[$middle-1]+$sorted[$middle])/2}
    $result['WarmMedianMs'] = [math]::Round($median,2)
    $result['WarmP95Ms'] = $sorted[[math]::Ceiling($sorted.Count*0.95)-1]
    $result['WarmSamples'] = $samples
    $json = $result | ConvertTo-Json -Depth 4
    $json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'metrics.json') -Encoding utf8
    $json
} finally {
    if ($process) {
        [ExileKitStartupProbe]::PostMessage([ExileKitStartupProbe]::Find($process.Id),0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
        $process.Dispose()
    }
}
