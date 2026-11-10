# Sediments - compilation automatique sous Windows
# Lance-le via COMPILER.bat (double-clic).

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

function Step($msg)  { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)    { Write-Host "    OK  $msg" -ForegroundColor Green }
function Fail($msg)  { Write-Host ""; Write-Host "PROBLEME : $msg" -ForegroundColor Red; Write-Host ""; exit 1 }

Write-Host "Sediments - compilation du plugin VST3" -ForegroundColor Yellow

if ($root.Length -gt 60) {
    Write-Host ""
    Write-Host "Attention : le dossier du projet a un chemin long :" -ForegroundColor Yellow
    Write-Host "  $root"
    Write-Host "Si la compilation echoue, deplace le dossier vers C:\Sediments et relance." -ForegroundColor Yellow
}

# 1. Visual Studio ---------------------------------------------------------
Step "Recherche de Visual Studio"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Fail "Visual Studio n'est pas installe. Installe 'Visual Studio Community' avec 'Developpement Desktop en C++' (voir README), puis relance."
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vsVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if (-not $vsPath) {
    Fail "Visual Studio est installe mais sans les outils C++. Ouvre 'Visual Studio Installer' > Modifier > coche 'Developpement Desktop en C++'."
}
$vsMajor = [int]($vsVersion.Split(".")[0])
if ($vsMajor -ge 18)     { $generator = "Visual Studio 18 2026" }
elseif ($vsMajor -eq 17) { $generator = "Visual Studio 17 2022" }
else { Fail "Visual Studio $vsVersion est trop ancien : il faut Visual Studio 2022 ou plus recent." }
Ok "$generator"

# 2. CMake (fourni avec Visual Studio) -------------------------------------
Step "Recherche de CMake"
$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { $cmake = $c.Source } else { Fail "CMake introuvable. Dans 'Visual Studio Installer', ajoute le composant 'Outils CMake C++ pour Windows'." }
}
Ok $cmake

# 3. Git (pour telecharger JUCE) -------------------------------------------
Step "Recherche de Git"
if (-not (Test-Path (Join-Path $root "JUCE\CMakeLists.txt"))) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        $vsGit = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd"
        if (Test-Path (Join-Path $vsGit "git.exe")) { $env:Path = "$vsGit;$env:Path" }
        else { Fail "Git est necessaire pour telecharger JUCE. Installe-le depuis https://git-scm.com/download/win (options par defaut), puis relance." }
    }
    Ok "Git disponible (JUCE sera telecharge automatiquement)"
} else {
    Ok "JUCE deja present"
}

# 4. libtorch --------------------------------------------------------------
Step "Recherche de libtorch"
$torch = $null
foreach ($cand in @("C:\libtorch", "C:\libtorch\libtorch", (Join-Path $root "libtorch"), (Join-Path $root "libtorch\libtorch"))) {
    if (Test-Path (Join-Path $cand "share\cmake\Torch\TorchConfig.cmake")) { $torch = $cand; break }
}
if (-not $torch) {
    Fail "libtorch introuvable. Telecharge-le (Windows / LibTorch / C++ / CPU, version Release) sur https://pytorch.org/get-started/locally/ et dezippe-le pour obtenir C:\libtorch\lib, C:\libtorch\include, etc."
}
if (Test-Path (Join-Path $torch "lib\torch_cpu.dll")) { Ok $torch }
else { Fail "Le dossier $torch ne contient pas lib\torch_cpu.dll : as-tu pris la version 'Release' pour Windows ?" }

# 5. Configuration ---------------------------------------------------------
Step "Preparation du projet (la premiere fois, telechargement de JUCE : quelques minutes)"
& $cmake -B build -G $generator -A x64 "-DCMAKE_PREFIX_PATH=$torch"
if ($LASTEXITCODE -ne 0) { Fail "La preparation a echoue. Copie les lignes rouges ci-dessus et envoie-les a Claude." }

# 6. Compilation -----------------------------------------------------------
Step "Compilation (5 a 15 minutes la premiere fois)"
& $cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { Fail "La compilation a echoue. Remonte jusqu'a la premiere ligne contenant 'error' et envoie-la a Claude." }

# 7. Rassembler le resultat ------------------------------------------------
Step "Copie du plugin"
$out = Join-Path $root "Plugin_pour_Reaper"
$vst3 = Join-Path $root "build\Sediments_artefacts\Release\VST3\Sediments.vst3"
if (-not (Test-Path $vst3)) { Fail "Le fichier VST3 n'a pas ete trouve apres la compilation." }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory $out | Out-Null
Copy-Item $vst3 $out -Recurse
Ok "Plugin copie dans : $out"

# 8. Rendre libtorch visible pour Reaper (variable Path de l'utilisateur) ---
Step "Acces de Reaper a libtorch"
$torchLib = Join-Path $torch "lib"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -and ($userPath.Split(";") -contains $torchLib)) {
    Ok "$torchLib est deja dans le Path"
} else {
    Write-Host "    Reaper a besoin de trouver les fichiers de libtorch."
    Write-Host "    Je peux ajouter $torchLib a ta variable Path (pour ton compte seulement)."
    $answer = Read-Host "    L'ajouter ? (O/N)"
    if ($answer -match "^[oOyY]") {
        if ($userPath) { $newPath = "$userPath;$torchLib" } else { $newPath = $torchLib }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Ok "Ajoute. Ferme completement Reaper et rouvre-le pour que ce soit pris en compte."
    } else {
        Write-Host "    Pas de changement. Voir le README pour le faire a la main." -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "TERMINE !" -ForegroundColor Green
Write-Host ""
Write-Host "Dans Reaper :"
Write-Host "  1. Options > Preferences > Plug-ins > VST"
Write-Host "  2. Clique 'Add...' et choisis le dossier :  $out"
Write-Host "  3. Clique 'Re-scan', puis OK"
Write-Host "  4. Sur une piste : FX > cherche 'Sediments'"
Write-Host ""
Write-Host "Test sans Reaper : build\Sediments_artefacts\Release\Standalone\Sediments.exe"
