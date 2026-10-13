# Launch a game copy, wait, and report whether it is still alive.
# Usage: smoke.ps1 [-Seconds 20] [-Keep] [-Dir testrun] [-GameArgs "/bwcursor"] [-Explorer]
# Automated tests use testrun (hidden via its ini); testgame is the player's copy.
param([int]$Seconds = 20, [switch]$Keep, [string]$Dir = "testrun", [string]$GameArgs = "", [switch]$Explorer)
$t = if ([System.IO.Path]::IsPathRooted($Dir)) { Resolve-Path $Dir } else { Join-Path $PSScriptRoot "..\$Dir" | Resolve-Path }
$exe = "$t\TP.exe"
$mine = Get-CimInstance Win32_Process -Filter "Name='TP.exe'" | Where-Object { $_.ExecutablePath -eq $exe }
if ($mine) { "refusing: $Dir is already running (PID $($mine.ProcessId))"; exit 1 }
$start = Get-Date
if ($Explorer) {
    # As the player launches it (on-screen tests): Explorer may bring a new window to the front.
    Start-Process explorer.exe -ArgumentList "`"$exe`""
    $p = $null
    for ($i = 0; $i -lt 50 -and -not $p; $i++) {
        Start-Sleep -Milliseconds 200
        $c = Get-CimInstance Win32_Process -Filter "Name='TP.exe'" | Where-Object { $_.ExecutablePath -eq "$exe" } | Select-Object -First 1
        if ($c) { $p = Get-Process -Id $c.ProcessId }
    }
    if (-not $p) { "explorer launch: TP.exe did not start"; exit 1 }
} else {
    $p = if ($GameArgs) { Start-Process -FilePath $exe -ArgumentList $GameArgs -WorkingDirectory $t -PassThru } else { Start-Process -FilePath $exe -WorkingDirectory $t -PassThru }
}
Start-Sleep $Seconds
$alive = -not $p.HasExited
$p.Refresh()
if ($alive) { "alive after ${Seconds}s: True" } else { "exited after <=${Seconds}s, code $($p.ExitCode)" }
$crash = Get-WinEvent -FilterHashtable @{LogName = 'Application'; Id = 1000; StartTime = $start } -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -match 'TP.exe' } | Select-Object -First 1
if ($crash) { ($crash.Message -split "`n" | Select-String 'Faulting module name|Fault offset|Exception code') -join "`n" }
if ($alive -and -not $Keep) { Stop-Process -Id $p.Id -Force }
