# =============================================================================
#  build.ps1 - NeuralStage (Windows, Visual Studio 17 2022 x64)
#
#  After a successful build, stages all three deliverables to
#  Installer\Binaries\ so NeuralStage.iss can find them without needing
#  to know the versioned artefact filenames.
#
#  Set $env:LAUNCH=1 to auto-launch the standalone after staging.
# =============================================================================
Set-Location $PSScriptRoot

# ---------- locate Visual Studio --------------------------------------------
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere not found. Install Visual Studio 2022."
    exit 1
}
$vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { Write-Error "Visual Studio 2022 not found."; exit 1 }

# ---------- cmake configure --------------------------------------------------
Write-Host "`n==> Configuring..." -ForegroundColor Cyan
cmake -S . -B Builds -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { Write-Error "cmake configure failed."; exit 1 }

# ---------- cmake build ------------------------------------------------------
Write-Host "`n==> Building Release..." -ForegroundColor Cyan
cmake --build Builds --config Release
if ($LASTEXITCODE -ne 0) { Write-Error "cmake build failed."; exit 1 }

# ---------- locate build outputs --------------------------------------------
$ver     = "0.2.1"
$exeSrc  = "$PSScriptRoot\Builds\NeuralStage_artefacts\Release\NeuralStage.v.$ver.exe"
$vst3Src = "$PSScriptRoot\Builds\NeuralStagePlugin_artefacts\Release\VST3\NeuralStage.vst3"
$clapSrc = "$PSScriptRoot\Builds\NeuralStagePlugin_artefacts\Release\CLAP\NeuralStage.v.$ver.clap"

foreach ($p in @($exeSrc, $vst3Src, $clapSrc)) {
    if (-not (Test-Path $p)) {
        Write-Error "Build output missing: $p"
        exit 1
    }
}
Write-Host "`n==> Built: $exeSrc" -ForegroundColor Green

# ---------- stage to Installer\Binaries\ ------------------------------------
# Wipe and recreate so stale artifacts from a prior version don't linger —
# especially the VST3 bundle, which is a folder (Copy-Item merges, not replaces).
$binDir = "$PSScriptRoot\Installer\Binaries"
Write-Host "`n==> Staging to $binDir ..." -ForegroundColor Cyan

if (Test-Path $binDir) { Remove-Item $binDir -Recurse -Force }
New-Item -ItemType Directory -Path $binDir | Out-Null

# Standalone exe — rename from versioned name to plain NeuralStage.exe
Copy-Item $exeSrc  "$binDir\NeuralStage.exe"

# VST3 bundle — copy the entire bundle folder tree
Copy-Item $vst3Src "$binDir\NeuralStage.vst3" -Recurse

# CLAP — rename from versioned name to plain NeuralStage.clap
Copy-Item $clapSrc "$binDir\NeuralStage.clap"

Write-Host "  EXE  : $binDir\NeuralStage.exe"    -ForegroundColor Green
Write-Host "  VST3 : $binDir\NeuralStage.vst3\"   -ForegroundColor Green
Write-Host "  CLAP : $binDir\NeuralStage.clap"    -ForegroundColor Green
Write-Host "`n==> Staging complete." -ForegroundColor Cyan
Write-Host "    To build the installer: iscc Installer\NeuralStage.iss" -ForegroundColor Cyan

# ---------- auto-install VST3 for immediate testing in REAPER -----------------
$vst3Dest = "C:\Program Files\Common Files\VST3\NeuralStage.vst3"
Write-Host "`n==> Installing VST3 to $vst3Dest ..." -ForegroundColor Cyan
try {
    if (Test-Path $vst3Dest) { Remove-Item $vst3Dest -Recurse -Force }
    Copy-Item "$binDir\NeuralStage.vst3" $vst3Dest -Recurse
    Write-Host "  VST3 installed: $vst3Dest" -ForegroundColor Green
} catch {
    Write-Warning "  VST3 auto-install failed (may need admin): $_"
    Write-Warning "  Manual copy: copy '$binDir\NeuralStage.vst3' to '$vst3Dest'"
}

if ("${env:LAUNCH}" -eq "1") { Start-Process "$binDir\NeuralStage.exe" }

# ---------- optional: upload Windows builds to a GitHub Release --------------
# Requires: gh CLI (winget install GitHub.cli) + gh auth login
# Usage: set $env:GH_RELEASE to a tag (e.g. "v0.2.1") before running build.ps1,
#        or pass it as an argument: build.ps1 -Release v0.2.1
param([string]$Release = $env:GH_RELEASE)

if ($Release) {
    $gh = (Get-Command gh -ErrorAction SilentlyContinue)?.Source
    if (-not $gh) {
        Write-Warning "gh CLI not found — skipping GitHub Release upload."
        Write-Warning "  Install: winget install GitHub.cli  then  gh auth login"
    } else {
        Write-Host "`n==> Uploading Windows builds to GitHub Release $Release ..." -ForegroundColor Cyan
        $winDir = "$PSScriptRoot\Installer\WIN"
        $uploads = Get-ChildItem $winDir -Filter "*.zip" | Select-Object -ExpandProperty FullName
        if ($uploads) {
            & $gh release upload $Release @uploads --clobber
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  Uploaded: $($uploads -join ', ')" -ForegroundColor Green
            } else {
                Write-Warning "  gh release upload failed (exit $LASTEXITCODE). Upload manually via GitHub web UI."
            }
        } else {
            Write-Warning "  No .zip files found in $winDir — run build + iscc first."
        }
    }
} else {
    Write-Host "`n==> To upload Windows builds to a GitHub Release:" -ForegroundColor DarkGray
    Write-Host "      Set-Variable GH_RELEASE v0.2.1 ; .\build.ps1 -Release v0.2.1" -ForegroundColor DarkGray
    Write-Host "    Or drag Installer\WIN\*.zip into the release on github.com/releases" -ForegroundColor DarkGray
}
