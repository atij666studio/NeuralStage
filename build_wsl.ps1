# =============================================================================
#  build_wsl.ps1 — Build NeuralStage for Linux from Windows via WSL
#
#  STATUS: WSL is currently unavailable on this machine.
#
#  The Windows 'VirtualMachinePlatform' and 'Microsoft-Windows-Subsystem-Linux'
#  optional features are not enabled, and the component store corruption
#  (error 14098) prevents enabling them via DISM. WSL 1 and WSL 2 both fail.
#
#  TO FIX WSL LOCALLY:
#    Run a Windows 11 in-place upgrade (keeps all files and apps):
#    1. Download the latest Windows 11 ISO from Microsoft.
#    2. Mount it and run setup.exe → "Keep personal files and apps".
#    3. After the upgrade completes, run:
#         wsl --install --no-distribution
#         wsl --import Ubuntu-22.04 "C:\WSL\Ubuntu-22.04" <rootfs.tar.gz> --version 2
#    4. Then run this script again.
#
#  ALTERNATIVE — USE GITHUB ACTIONS (fully working, no local WSL needed):
#    The repo includes .github/workflows/build-linux.yml which automatically
#    builds Linux x86_64 AND ARM64 (Raspberry Pi 5) on every push.
#
#    Workflow:
#      1. Push to GitHub:     git push origin main
#      2. Go to the Actions tab in GitHub and download the artifacts when done:
#         - NeuralStage-Linux-x86_64-standalone
#         - NeuralStage-Linux-ARM64-standalone   ← for Raspberry Pi 5
#      3. Deploy to Pi 5 (replace PI_IP):
#         scp NeuralStage.v.0.2.1 pi@PI_IP:~/NeuralStage/NeuralStage
#
#  The rootfs tarball was already downloaded to:
#    $env:TEMP\ubuntu-22.04-wsl.rootfs.tar.gz  (325 MB, ready to import)
#  Once the in-place upgrade is done, run:
#    wsl --import Ubuntu-22.04 "C:\WSL\Ubuntu-22.04" "$env:TEMP\ubuntu-22.04-wsl.rootfs.tar.gz" --version 2
#    wsl --set-default Ubuntu-22.04
# =============================================================================

Write-Host ""
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host "  WSL IS NOT AVAILABLE ON THIS MACHINE" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""
Write-Host "The 'VirtualMachinePlatform' Windows feature cannot be enabled" -ForegroundColor White
Write-Host "due to component store corruption (error 14098 / 0x80073712)." -ForegroundColor White
Write-Host ""
Write-Host "OPTION 1 — Fix WSL locally:" -ForegroundColor Cyan
Write-Host "  Run a Windows 11 in-place upgrade (keeps all files)," -ForegroundColor Gray
Write-Host "  then re-run this script." -ForegroundColor Gray
Write-Host ""
Write-Host "OPTION 2 — Use GitHub Actions (recommended, no WSL needed):" -ForegroundColor Cyan
Write-Host "  git push origin main" -ForegroundColor Green
Write-Host "  -> Download artifacts from the GitHub Actions tab:" -ForegroundColor Gray
Write-Host "     NeuralStage-Linux-x86_64-standalone" -ForegroundColor Gray
Write-Host "     NeuralStage-Linux-ARM64-standalone  (Raspberry Pi 5)" -ForegroundColor Gray
Write-Host "  -> scp NeuralStage.v.0.2.1 pi@<PI_IP>:~/NeuralStage/NeuralStage" -ForegroundColor Gray
Write-Host ""
Write-Host "The Ubuntu 22.04 rootfs is ready at:" -ForegroundColor White
Write-Host "  $env:TEMP\ubuntu-22.04-wsl.rootfs.tar.gz" -ForegroundColor Gray
Write-Host "Import it after fixing WSL:" -ForegroundColor White
Write-Host '  wsl --import Ubuntu-22.04 "C:\WSL\Ubuntu-22.04" "$env:TEMP\ubuntu-22.04-wsl.rootfs.tar.gz" --version 2' -ForegroundColor Gray
Write-Host '  wsl --set-default Ubuntu-22.04' -ForegroundColor Gray
Write-Host ""

exit 1
