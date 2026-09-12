param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Test,
    [string[]]$Targets = @(),
    [string]$TestRegex = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
function Invoke-BuildTool([string]$File, [string[]]$Arguments) {
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $File
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    # Some desktop hosts inject both PATH and Path; MSBuild's .NET Framework rejects them.
    $buildPath = $env:Path
    foreach ($key in @($info.Environment.Keys)) {
        if ($key -ieq 'path') { $info.Environment.Remove($key) | Out-Null }
    }
    $info.Environment['Path'] = $buildPath
    $process = [System.Diagnostics.Process]::Start($info)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Output $stdout.GetAwaiter().GetResult()
    Write-Output $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "$File failed with exit code $($process.ExitCode)" }
    $process.Dispose()
}
Invoke-BuildTool $cmake @('--preset', 'windows-msvc')
$buildArguments = @('--build', '--preset', $Configuration.ToLowerInvariant())
if ($Targets.Count) { $buildArguments += '--target'; $buildArguments += $Targets }
Invoke-BuildTool $cmake $buildArguments
if ($Test) {
    $testArguments = @('--preset', $Configuration.ToLowerInvariant())
    if ($TestRegex) { $testArguments += @('--tests-regex', $TestRegex) }
    Invoke-BuildTool (Join-Path (Split-Path $cmake) 'ctest.exe') $testArguments
}
