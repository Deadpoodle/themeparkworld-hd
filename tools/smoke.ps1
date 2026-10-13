# Launch a game copy, wait, and report whether it is still alive.
# Usage: smoke.ps1 [-Seconds 20] [-Keep] [-Dir testrun] [-GameArgs "/bwcursor"]
# Automated tests use testrun (hidden via its ini); testgame is the player's copy.
param([int]$Seconds = 20, [switch]$Keep, [string]$Dir = "testrun", [string]$GameArgs = "")
$t = if ([System.IO.Path]::IsPathRooted($Dir)) { Resolve-Path $Dir } else { Join-Path $PSScriptRoot "..\$Dir" | Resolve-Path }
$exe = "$t\TP.exe"
$mine = Get-CimInstance Win32_Process -Filter "Name='TP.exe'" | Where-Object { $_.ExecutablePath -eq $exe }
if ($mine) { "refusing: $Dir is already running (PID $($mine.ProcessId))"; exit 1 }
$start = Get-Date
$p = if ($GameArgs) { Start-Process -FilePath $exe -ArgumentList $GameArgs -WorkingDirectory $t -PassThru } else { Start-Process -FilePath $exe -WorkingDirectory $t -PassThru }
Start-Sleep $Seconds
$alive = -not $p.HasExited
if ($alive) { "alive after ${Seconds}s: True" } else { "exited after <=${Seconds}s, code $($p.ExitCode)" }
$crash = Get-WinEvent -FilterHashtable @{LogName = 'Application'; Id = 1000; StartTime = $start } -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -match 'TP.exe' } | Select-Object -First 1
if ($crash) { ($crash.Message -split "`n" | Select-String 'Faulting module name|Fault offset|Exception code') -join "`n" }
if ($alive -and -not $Keep) { Stop-Process -Id $p.Id -Force }
