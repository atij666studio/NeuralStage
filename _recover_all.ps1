$hist = "$env:APPDATA\Code\User\History"
$count = 0
$skipped = 0
Get-ChildItem $hist -Directory | ForEach-Object {
    $dir = $_.FullName
    $ej = Join-Path $dir 'entries.json'
    if (-not (Test-Path $ej)) { return }
    try { $j = Get-Content $ej -Raw | ConvertFrom-Json } catch { return }
    $res = $j.resource
    if ($res -notmatch 'AtiNAMatiC-NeuralStage/Source/') { return }
    $target = [Uri]::UnescapeDataString(($res -replace '^file:///','')) -replace '/','\'
    $latest = Get-ChildItem $dir -File | Where-Object { $_.Name -ne 'entries.json' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { return }
    $tdir = Split-Path $target -Parent
    if (-not (Test-Path $tdir)) { New-Item -ItemType Directory -Path $tdir -Force | Out-Null }
    # Only overwrite if snapshot is newer than existing target (or target missing)
    $existing = Get-Item $target -ErrorAction SilentlyContinue
    if ($existing -and $existing.LastWriteTime -ge $latest.LastWriteTime) { $script:skipped++; return }
    Copy-Item $latest.FullName $target -Force
    Write-Host ("RESTORED {0:MM-dd HH:mm}  {1}" -f $latest.LastWriteTime, ($target -replace '.*\\Source\\','Source\'))
    $script:count++
}
Write-Host "----"
Write-Host "Restored: $count   Skipped (target newer/equal): $skipped"
