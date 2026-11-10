# Sediments - automatic build on Windows
# Run it through BUILD.bat (double-click).

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

function Step($msg)  { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)    { Write-Host "    OK  $msg" -ForegroundColor Green }
function Fail($msg)  { Write-Host ""; Write-Host "PROBLEM: $msg" -ForegroundColor Red; Write-Host ""; exit 1 }

Write-Host "Sediments - building the VST3 plugin" -ForegroundColor Yellow

if ($root.Length -gt 60) {
    Write-Host ""
    Write-Host "Warning: the project folder has a long path:" -ForegroundColor Yellow
    Write-Host "  $root"
    Write-Host "If the build fails, move the folder to C:\Sediments and run it again." -ForegroundColor Yellow
}

# 1. Visual Studio ---------------------------------------------------------
Step "Looking for Visual Studio"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Fail "Visual Studio is not installed. Install 'Visual Studio Community' with 'Desktop development with C++' (see README), then run this again."
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vsVersion = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion
if (-not $vsPath) {
    Fail "Visual Studio is installed but without the C++ tools. Open 'Visual Studio Installer' > Modify > tick 'Desktop development with C++'."
}
$vsMajor = [int]($vsVersion.Split(".")[0])
if ($vsMajor -ge 18)     { $generator = "Visual Studio 18 2026" }
elseif ($vsMajor -eq 17) { $generator = "Visual Studio 17 2022" }
else { Fail "Visual Studio $vsVersion is too old: Visual Studio 2022 or later is required." }
Ok "$generator"

# 2. CMake (comes with Visual Studio) --------------------------------------
Step "Looking for CMake"
$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { $cmake = $c.Source } else { Fail "CMake not found. In 'Visual Studio Installer', add the 'C++ CMake tools for Windows' component." }
}
Ok $cmake

# 3. Git (to download JUCE) ------------------------------------------------
Step "Looking for Git"
if (-not (Test-Path (Join-Path $root "JUCE\CMakeLists.txt"))) {
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        $vsGit = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\cmd"
        if (Test-Path (Join-Path $vsGit "git.exe")) { $env:Path = "$vsGit;$env:Path" }
        else { Fail "Git is needed to download JUCE. Install it from https://git-scm.com/download/win (default options), then run this again." }
    }
    Ok "Git available (JUCE will be downloaded automatically)"
} else {
    Ok "JUCE already present"
}

# 4. libtorch --------------------------------------------------------------
Step "Looking for libtorch"
$torch = $null
foreach ($cand in @("C:\libtorch", "C:\libtorch\libtorch", (Join-Path $root "libtorch"), (Join-Path $root "libtorch\libtorch"))) {
    if (Test-Path (Join-Path $cand "share\cmake\Torch\TorchConfig.cmake")) { $torch = $cand; break }
}
if (-not $torch) {
    Fail "libtorch not found. Download it (Windows / LibTorch / C++ / CPU, Release version) from https://pytorch.org/get-started/locally/ and unzip it so that you get C:\libtorch\lib, C:\libtorch\include, etc."
}
if (Test-Path (Join-Path $torch "lib\torch_cpu.dll")) { Ok $torch }
else { Fail "The folder $torch does not contain lib\torch_cpu.dll: did you download the Windows 'Release' version?" }

# 5. Configuration ---------------------------------------------------------
Step "Preparing the project (the first time, JUCE is downloaded: a few minutes)"
& $cmake -B build -G $generator -A x64 "-DCMAKE_PREFIX_PATH=$torch"
if ($LASTEXITCODE -ne 0) { Fail "Preparation failed. Copy the red lines above and send them to Claude." }

# 6. Build -----------------------------------------------------------------
Step "Building (5 to 15 minutes the first time)"
& $cmake --build build --config Release --parallel
if ($LASTEXITCODE -ne 0) { Fail "The build failed. Scroll up to the first line containing 'error' and send it to Claude." }

# 7. Gather the result -----------------------------------------------------
Step "Copying the plugin"
$out = Join-Path $root "Plugin_for_Reaper"
$vst3 = Join-Path $root "build\Sediments_artefacts\Release\VST3\Sediments.vst3"
if (-not (Test-Path $vst3)) { Fail "The VST3 file was not found after the build." }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory $out | Out-Null
Copy-Item $vst3 $out -Recurse
Ok "Plugin copied to: $out"

# 8. Make libtorch visible to Reaper (user Path variable) ------------------
Step "Letting Reaper find libtorch"
$torchLib = Join-Path $torch "lib"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -and ($userPath.Split(";") -contains $torchLib)) {
    Ok "$torchLib is already in the Path"
} else {
    Write-Host "    Reaper needs to find the libtorch files."
    Write-Host "    I can add $torchLib to your Path variable (for your account only)."
    $answer = Read-Host "    Add it? (Y/N)"
    if ($answer -match "^[yYoO]") {
        if ($userPath) { $newPath = "$userPath;$torchLib" } else { $newPath = $torchLib }
        [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
        Ok "Added. Close Reaper completely and reopen it for this to take effect."
    } else {
        Write-Host "    No change. See the README to do it by hand." -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "DONE!" -ForegroundColor Green
Write-Host ""
Write-Host "In Reaper:"
Write-Host "  1. Options > Preferences > Plug-ins > VST"
Write-Host "  2. Click 'Add...' and choose the folder:  $out"
Write-Host "  3. Click 'Re-scan', then OK"
Write-Host "  4. On a track: FX > search for 'Sediments'"
Write-Host ""
Write-Host "Test without Reaper: build\Sediments_artefacts\Release\Standalone\Sediments.exe"
