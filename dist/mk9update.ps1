# MK9 Access updater for the port (machinebreak/mk9-lauta).
#
# The mod starts this hidden every time the game starts (src/update.c), however
# the game was launched, with -WaitPid <the game's process id>:
#   1. asks GitHub for the latest release; stops here if the port is current.
#   2. downloads and unpacks the windows-x64 zip into MK9Access\pending\ while
#      the game runs, then writes "ready X.Y.Z" to MK9Access\update_status.txt
#      (the mod speaks: "Game update X.Y.Z is ready. It installs when you close
#      the game").
#   3. waits until the game (every mk9.exe from this folder) has closed, then
#      copies the new files over the game and writes "applied X.Y.Z" (the mod
#      says "Game updated to version X.Y.Z" on the next start).
# Without -WaitPid (run by hand with the game closed) it installs at once.
#
# First install: "MK9 Access launch.cmd" runs this in its console window when
# the folder has no mk9.exe yet (the mod zip was unzipped into an empty
# folder). It then downloads the latest port, unpacks it here and says each
# step in the console, so the screen reader reads it.
#
# The port's own in-game banner is turned off (mk9_update_check = false): it
# only works with a mouse, and its installer failed here (in-use DLLs).
# Kept: mk9.toml (mk9_plugins = true, the user's settings) and everything the
# release zip does not contain (saves, game_root, MK9Access, plugins).
# A failed copy puts the replaced files back. Log: MK9Access\update.log.

param([string]$GameDir, [int]$WaitPid = 0, [string]$Installed = '')

$ErrorActionPreference = 'Stop'
$repo = 'machinebreak/mk9-lauta'
$keep = @('mk9.toml')                                   # release files we must NOT overwrite
$headers = @{ 'User-Agent' = 'MK9Access-Updater' }      # generic UA, no personal data

if (-not $GameDir) { $GameDir = Split-Path -Parent $PSScriptRoot }   # MK9Access\ -> game\
$GameDir = (Resolve-Path $GameDir).Path.TrimEnd('\')
$modDir  = Join-Path $GameDir 'MK9Access'
$status  = Join-Path $modDir 'update_status.txt'
$logFile = Join-Path $modDir 'update.log'

function Log($msg) {
    try {
        Add-Content -Path $logFile -Encoding utf8 -Value ((Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ' + $msg)
        $lines = Get-Content -Path $logFile
        if ($lines.Count -gt 300) { Set-Content -Path $logFile -Encoding utf8 -Value ($lines | Select-Object -Last 200) }
    } catch {}
}
$fresh = -not (Test-Path (Join-Path $GameDir 'mk9.exe'))   # first install: no port in this folder yet
function Say($msg) { Log $msg; if ($fresh) { Write-Host $msg } }
function Set-Status($text) { if (-not $fresh) { Set-Content -Path $status -Encoding ascii -Value $text } }

function Get-LoggedVersion {
    # the port logs "this program X.Y.Z" every launch; read the newest log.
    try {
        $log = Get-ChildItem -Path (Join-Path $GameDir 'logs') -Filter 'mk9_*.log' |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($log) {
            $m = Select-String -Path $log.FullName -Pattern 'this program (\d+\.\d+\.\d+)' | Select-Object -Last 1
            if ($m) { return $m.Matches[0].Groups[1].Value }
        }
    } catch {}
    return $null
}

function Test-GameRunning {
    # any mk9.exe started from this folder (the one that started us, or a new start meanwhile)
    $exe = Join-Path $GameDir 'mk9.exe'
    foreach ($p in @(Get-Process -Name mk9 -ErrorAction SilentlyContinue)) {
        try { if ($p.Path -and ($p.Path -ieq $exe)) { return $true } } catch { return $true }
    }
    return $false
}

# One updater at a time: a second game start while one waits just leaves it to the first.
$lock = $null
try {
    $lock = [IO.File]::Open((Join-Path $modDir 'update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch { exit 0 }

try {
    if ($fresh) { $Installed = '0.0.0'; Say 'MK9 by Lauta is not in this folder yet. Asking GitHub for its latest version.' }
    if (-not $Installed) { $Installed = Get-LoggedVersion }
    if (-not $Installed) { Log 'no installed version known yet: nothing to do'; exit 0 }
    $installedV = [version]$Installed

    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -Headers $headers -TimeoutSec 20
    $latest = ($rel.tag_name -replace '^v', '')
    $latestV = [version]$latest
    $pendingRoot = Join-Path $modDir 'pending'
    if ($latestV -le $installedV) {
        Log "check: port $Installed is current (latest $latest)"
        if (Test-Path $pendingRoot) { Remove-Item -Path $pendingRoot -Recurse -Force -ErrorAction SilentlyContinue }
        # a stale "ready"/"failed" (installed some other way meanwhile) must not be spoken; "applied" is the mod's to clear
        if ((Test-Path $status) -and ((Get-Content -Path $status -Raw) -match '^(ready|failed) ')) { Remove-Item -Path $status -Force }
        exit 0
    }

    # 2. download + unpack while the game runs (kept across starts until installed)
    $pending = Join-Path $pendingRoot $latest
    $files   = Join-Path $pending 'files'
    $done    = Join-Path $pending 'complete'
    if (-not (Test-Path $done)) {
        $asset = $rel.assets | Where-Object { $_.name -like '*windows-x64.zip' } | Select-Object -First 1
        if (-not $asset) { Log "check: $latest has no windows-x64.zip"; if ($fresh) { throw "version $latest has no Windows download yet" }; exit 0 }
        Log "update $Installed -> $latest : downloading $($asset.name)"
        if ($fresh) { Write-Host ("Downloading MK9 by Lauta $latest, {0} MB. This can take a few minutes." -f [math]::Round($asset.size / 1MB)) }
        if (Test-Path $pendingRoot) { Remove-Item -Path $pendingRoot -Recurse -Force }
        New-Item -ItemType Directory -Path $pending -Force | Out-Null
        $zip = Join-Path $pending 'port.zip'
        $ProgressPreference = 'SilentlyContinue'                  # PS 5.1's progress bar slows downloads a lot
        Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -Headers $headers -TimeoutSec 1800 -UseBasicParsing
        if ((Get-Item $zip).Length -ne $asset.size) { throw "download is $((Get-Item $zip).Length) bytes, expected $($asset.size)" }
        if ($fresh) { Write-Host 'Download finished. Unpacking.' }
        Expand-Archive -Path $zip -DestinationPath $files -Force
        Remove-Item -Path $zip -Force
        # the release lays files at the zip root; be robust if a future zip wraps them in a folder.
        $exe = Get-ChildItem -Path $files -Recurse -Filter 'mk9.exe' | Select-Object -First 1
        if (-not $exe) { throw 'the zip has no mk9.exe' }
        Set-Content -Path $done -Encoding ascii -Value (Split-Path -Parent $exe.FullName)
        Log "update $latest downloaded and unpacked"
    }
    $root = (Get-Content -Path $done | Select-Object -First 1)
    Set-Status "ready $latest"

    # 3. install once the game has closed
    if ($WaitPid) {
        try { Wait-Process -Id $WaitPid -ErrorAction SilentlyContinue } catch {}
        while (Test-GameRunning) { Start-Sleep -Seconds 2 }
        Start-Sleep -Seconds 1                                    # let Windows release the exe and DLLs
    } elseif (Test-GameRunning) {
        Log "update $latest waits: the game is running"; exit 0
    }

    $backup = Join-Path $pending 'backup'
    $copied = New-Object System.Collections.Generic.List[string]
    try {
        # mk9.exe first: if it is still locked nothing else has been touched yet.
        $items = Get-ChildItem -Path $root -Recurse -File | Sort-Object { if ($_.Name -ieq 'mk9.exe') { 0 } else { 1 } }
        foreach ($f in $items) {
            $relPath = $f.FullName.Substring($root.Length).TrimStart('\')
            $target = Join-Path $GameDir $relPath
            if (($keep -contains $relPath) -and (Test-Path $target)) { continue }   # a first install takes the port's mk9.toml
            if (Test-Path $target) {
                $b = Join-Path $backup $relPath
                New-Item -ItemType Directory -Path (Split-Path -Parent $b) -Force | Out-Null
                Copy-Item -Path $target -Destination $b -Force
            } else {
                New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            }
            $copied.Add($relPath)
            Copy-Item -Path $f.FullName -Destination $target -Force
        }
    } catch {
        $why = $_.Exception.Message
        foreach ($relPath in $copied) {                               # put the replaced files back
            $b = Join-Path $backup $relPath
            try {
                if (Test-Path $b) { Copy-Item -Path $b -Destination (Join-Path $GameDir $relPath) -Force }
                else { Remove-Item -Path (Join-Path $GameDir $relPath) -Force }
            } catch {}
        }
        if (Test-Path $backup) { Remove-Item -Path $backup -Recurse -Force -ErrorAction SilentlyContinue }
        Log "update $latest could not be installed, game left as it was: $why"
        Set-Status "failed $latest"
        exit 1
    }
    Remove-Item -Path $pendingRoot -Recurse -Force -ErrorAction SilentlyContinue
    Set-Status "applied $latest"
    Log "update to $latest installed ($($copied.Count) files)"
    if ($fresh) { Write-Host "MK9 by Lauta $latest is installed. Starting the game." }
} catch {
    Log ('update check failed: ' + $_.Exception.Message)   # offline, GitHub down: try again next start
    if ($fresh) { Write-Host ('The download did not work: ' + $_.Exception.Message); exit 1 }
} finally {
    if ($lock) { $lock.Close() }
}
