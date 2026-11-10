# Sediments - Windows installer (run INSTALL.bat)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Sediments - installation" -ForegroundColor Yellow
Write-Host ""

if (Get-Process -Name "reaper" -ErrorAction SilentlyContinue) {
    Write-Host "Reaper is open. Close it completely, then run INSTALL.bat again." -ForegroundColor Red
    exit 1
}

# Per-user VST3 folder (no administrator rights needed)
$vst3Dir  = Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
$torchDir = Join-Path $env:LOCALAPPDATA "Sediments\libtorch"
New-Item -ItemType Directory -Force $vst3Dir  | Out-Null
New-Item -ItemType Directory -Force $torchDir | Out-Null

Write-Host "==> Copying the plugin" -ForegroundColor Cyan
$dest = Join-Path $vst3Dir "Sediments.vst3"
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
Copy-Item (Join-Path $here "Sediments.vst3") $vst3Dir -Recurse
Write-Host "    $dest"

Write-Host "==> Copying libtorch (the engine that runs the models, about 1 minute)" -ForegroundColor Cyan
Copy-Item (Join-Path $here "libtorch\*") $torchDir -Force
Write-Host "    $torchDir"

# Put libtorch first in the user's Path so that Reaper finds it
Write-Host "==> Letting Reaper find libtorch" -ForegroundColor Cyan
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$parts = @()
if ($userPath) { $parts = $userPath.Split(";") | Where-Object { $_ -and ($_ -ne $torchDir) } }
$newPath = (@($torchDir) + $parts) -join ";"
[Environment]::SetEnvironmentVariable("Path", $newPath, "User")
Write-Host "    OK"

Write-Host ""
Write-Host "DONE!" -ForegroundColor Green
Write-Host ""
Write-Host "In Reaper (first time only):"
Write-Host "  1. Options > Preferences > Plug-ins > VST"
Write-Host "  2. If the following folder is not in the list yet, click 'Add...' and choose it:"
Write-Host "     $vst3Dir"
Write-Host "  3. Click 'Re-scan', then OK"
Write-Host "  4. On a track: FX > search for 'Sediments'"
Write-Host ""
Write-Host "To update: close Reaper and simply run INSTALL.bat again."
