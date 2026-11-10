# Sediments - installation pour Windows (lance INSTALLER.bat)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "Sediments - installation" -ForegroundColor Yellow
Write-Host ""

if (Get-Process -Name "reaper" -ErrorAction SilentlyContinue) {
    Write-Host "Reaper est ouvert. Ferme-le completement, puis relance INSTALLER.bat." -ForegroundColor Red
    exit 1
}

# Dossier VST3 de l'utilisateur (pas besoin des droits administrateur)
$vst3Dir  = Join-Path $env:LOCALAPPDATA "Programs\Common\VST3"
$torchDir = Join-Path $env:LOCALAPPDATA "Sediments\libtorch"
New-Item -ItemType Directory -Force $vst3Dir  | Out-Null
New-Item -ItemType Directory -Force $torchDir | Out-Null

Write-Host "==> Copie du plugin" -ForegroundColor Cyan
$dest = Join-Path $vst3Dir "Sediments.vst3"
if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
Copy-Item (Join-Path $here "Sediments.vst3") $vst3Dir -Recurse
Write-Host "    $dest"

Write-Host "==> Copie de libtorch (le moteur des modeles, ~1 minute)" -ForegroundColor Cyan
Copy-Item (Join-Path $here "libtorch\*") $torchDir -Force
Write-Host "    $torchDir"

# Mettre libtorch en tete du Path de l'utilisateur, pour que Reaper le trouve
Write-Host "==> Acces de Reaper a libtorch" -ForegroundColor Cyan
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$parts = @()
if ($userPath) { $parts = $userPath.Split(";") | Where-Object { $_ -and ($_ -ne $torchDir) } }
$newPath = (@($torchDir) + $parts) -join ";"
[Environment]::SetEnvironmentVariable("Path", $newPath, "User")
Write-Host "    OK"

Write-Host ""
Write-Host "TERMINE !" -ForegroundColor Green
Write-Host ""
Write-Host "Dans Reaper (la premiere fois seulement) :"
Write-Host "  1. Options > Preferences > Plug-ins > VST"
Write-Host "  2. Si le dossier suivant n'est pas deja dans la liste, clique 'Add...' et choisis-le :"
Write-Host "     $vst3Dir"
Write-Host "  3. Clique 'Re-scan', puis OK"
Write-Host "  4. Sur une piste : FX > cherche 'Sediments'"
Write-Host ""
Write-Host "Pour une mise a jour : ferme Reaper et relance simplement INSTALLER.bat."
