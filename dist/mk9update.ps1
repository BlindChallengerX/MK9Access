# MK9 Access updater: keeps both the port (machinebreak/mk9-lauta) and the mod
# itself (BlindChallengerX/MK9Access) on their latest GitHub releases.
#
# The mod starts this hidden every time the game starts (src/update.c), however
# the game was launched, with -WaitPid <the game's process id> -Installed <port
# version> -ModVersion <mod version>:
#   1. asks GitHub for the latest release of each; stops here if both are current.
#   2. downloads and unpacks each newer zip into MK9Access\pending\<port|mod>\
#      while the game runs, then writes "ready X.Y.Z" to its status file:
#        port: MK9Access\update_status.txt      ("Game update X.Y.Z is ready ...")
#        mod:  MK9Access\mod_update_status.txt  ("MK9 Access update X.Y.Z is ready ...")
#      Both files are read and spoken by the mod.
#   3. waits until the game (every mk9.exe from this folder) has closed, then
#      copies the new files over the game folder (port first, then the mod) and
#      writes "applied X.Y.Z" (said on the next start) or "failed X.Y.Z".
# Without -WaitPid (run by hand with the game closed) it installs at once. Without
# -ModVersion the mod version is read from plugins\mk9access.dll (its VERSIONINFO).
#
# First install: "MK9 Access launch.cmd" runs this in its console window when
# the folder has no mk9.exe yet (the mod zip was unzipped into an empty
# folder). It then downloads the latest port only (the mod was just unzipped),
# unpacks it here and says each step in the console for the screen reader.
#
# The port's own in-game banner is turned off (mk9_update_check = false): it
# only works with a mouse, and its installer failed here (in-use DLLs).
# Kept: mk9.toml (mk9_plugins = true, the user's settings) and everything the
# release zips do not contain (saves, game_root, the mod's own settings).
# The updater replaces itself too: PowerShell has read the whole script before
# it runs, so copying a new mk9update.ps1 over this one is safe.
# A failed copy puts the replaced files back. Log: MK9Access\update.log.
# Testing only: MK9A_MOD_FEED = path of a local JSON file shaped like GitHub's
# releases/latest answer (tag_name, assets[].name/size/browser_download_url as a
# local path), used instead of the mod's GitHub release.

param([string]$GameDir, [int]$WaitPid = 0, [string]$Installed = '', [string]$ModVersion = '')

$ErrorActionPreference = 'Stop'
$headers = @{ 'User-Agent' = 'MK9Access-Updater' }      # generic UA, no personal data
# GitHub only takes TLS 1.2+; PowerShell 5.1 on some Windows setups still offers only TLS 1.0 by default.
try { [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12 } catch {}

if (-not $GameDir) { $GameDir = Split-Path -Parent $PSScriptRoot }   # MK9Access\ -> game\
$GameDir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\')
$modDir  = Join-Path $GameDir 'MK9Access'
$logFile = Join-Path $modDir 'update.log'
$pendingRoot = Join-Path $modDir 'pending'

function Log($msg) {
    try {
        Add-Content -LiteralPath $logFile -Encoding utf8 -Value ((Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' ' + $msg)
        $lines = Get-Content -LiteralPath $logFile
        if ($lines.Count -gt 300) { Set-Content -LiteralPath $logFile -Encoding utf8 -Value ($lines | Select-Object -Last 200) }
    } catch {}
}
$fresh = -not (Test-Path -LiteralPath (Join-Path $GameDir 'mk9.exe'))   # first install: no port in this folder yet
function Say($msg) { Log $msg; if ($fresh) { Write-Host $msg } }

function Get-LoggedVersion {
    # the port logs "this program X.Y.Z" every launch; read the newest log.
    try {
        $log = Get-ChildItem -LiteralPath (Join-Path $GameDir 'logs') -Filter 'mk9_*.log' |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($log) {
            $m = Select-String -LiteralPath $log.FullName -Pattern 'this program (\d+\.\d+\.\d+)' | Select-Object -Last 1
            if ($m) { return $m.Matches[0].Groups[1].Value }
        }
    } catch {}
    return $null
}

function Get-ModDllVersion {
    try {
        $v = (Get-Item -LiteralPath (Join-Path $GameDir 'plugins\mk9access.dll')).VersionInfo.ProductVersion
        if ($v -match '^\d+\.\d+\.\d+') { return $Matches[0] }
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

# What gets updated. Marker = a file every release zip has, relative to the zip's
# root (= the game folder); it finds the root if a zip ever wraps it in a folder.
$port = @{ Name = 'port'; Label = 'MK9 by Lauta'; Repo = 'machinebreak/mk9-lauta'; Asset = '*windows-x64.zip'
           Marker = 'mk9.exe'; Keep = @('mk9.toml'); Status = (Join-Path $modDir 'update_status.txt'); Installed = $Installed }
$mod  = @{ Name = 'mod'; Label = 'MK9 Access'; Repo = 'BlindChallengerX/MK9Access'; Asset = 'MK9Access-*.zip'
           Marker = 'plugins\mk9access.dll'; Keep = @(); Status = (Join-Path $modDir 'mod_update_status.txt'); Installed = $ModVersion
           Feed = $env:MK9A_MOD_FEED }

function Set-Status($c, $text) { if (-not $fresh) { Set-Content -LiteralPath $c.Status -Encoding ascii -Value $text } }

function Get-Release($c) {
    if ($c.Feed) { return (Get-Content -LiteralPath $c.Feed -Raw | ConvertFrom-Json) }
    return Invoke-RestMethod -Uri "https://api.github.com/repos/$($c.Repo)/releases/latest" -Headers $headers -TimeoutSec 20
}

function Save-Asset($c, $asset, $zip) {
    if ($c.Feed) { Copy-Item -LiteralPath $asset.browser_download_url -Destination $zip -Force; return }
    # WebClient, not Invoke-WebRequest: -OutFile reads [ ] in a folder name as a wildcard and fails
    $wc = New-Object Net.WebClient
    try {
        $wc.Headers['User-Agent'] = $headers['User-Agent']
        $wc.DownloadFile($asset.browser_download_url, $zip)
    } finally { $wc.Dispose() }
}

function Expand-Zip($zip, $dest) {
    # .NET's unzip, not Expand-Archive: that one fails when the folder name has [ ]
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $dest)
}

# Step 1 + 2 for one component: returns the unpacked root to install from, or $null (current, offline, no asset).
function Get-Pending($c) {
    try {
        if (-not $c.Installed) { Log "$($c.Name): no installed version known yet: nothing to do"; return $null }
        $installedV = [version]$c.Installed
        $rel = Get-Release $c
        $latest = ($rel.tag_name -replace '^v', '')
        $latestV = [version]$latest
        $croot = Join-Path $pendingRoot $c.Name
        if ($latestV -le $installedV) {
            Log "check: $($c.Name) $($c.Installed) is current (latest $latest)"
            if (Test-Path -LiteralPath $croot) { Remove-Item -LiteralPath $croot -Recurse -Force -ErrorAction SilentlyContinue }
            # a stale "ready"/"failed" (installed some other way meanwhile) must not be spoken; "applied" is the mod's to clear
            if ((Test-Path -LiteralPath $c.Status) -and ((Get-Content -LiteralPath $c.Status -Raw) -match '^(ready|failed) ')) { Remove-Item -LiteralPath $c.Status -Force }
            return $null
        }
        $pending = Join-Path $croot $latest
        $files   = Join-Path $pending 'files'
        $done    = Join-Path $pending 'complete'
        if (-not (Test-Path -LiteralPath $done)) {                             # kept across starts until installed
            $asset = $rel.assets | Where-Object { $_.name -like $c.Asset } | Select-Object -First 1
            if (-not $asset) { Log "check: $($c.Name) $latest has no $($c.Asset)"; if ($fresh) { throw "version $latest has no Windows download yet" }; return $null }
            Log "update $($c.Name) $($c.Installed) -> $latest : downloading $($asset.name)"
            if ($fresh) { Write-Host ("Downloading $($c.Label) $latest, {0} MB. This can take a few minutes." -f [math]::Round($asset.size / 1MB)) }
            if (Test-Path -LiteralPath $croot) { Remove-Item -LiteralPath $croot -Recurse -Force }
            New-Item -ItemType Directory -Path $pending -Force | Out-Null
            $zip = Join-Path $pending 'update.zip'
            Save-Asset $c $asset $zip
            if ((Get-Item -LiteralPath $zip).Length -ne $asset.size) { throw "download is $((Get-Item -LiteralPath $zip).Length) bytes, expected $($asset.size)" }
            if ($fresh) { Write-Host 'Download finished. Unpacking.' }
            Expand-Zip $zip $files
            Remove-Item -LiteralPath $zip -Force
            $leaf = Split-Path -Leaf $c.Marker
            $hit = Get-ChildItem -LiteralPath $files -Recurse -Filter $leaf |
                   Where-Object { $_.FullName.EndsWith('\' + $c.Marker, [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
            if (-not $hit) { throw "the zip has no $($c.Marker)" }
            $root = $hit.FullName.Substring(0, $hit.FullName.Length - $c.Marker.Length).TrimEnd('\')
            Set-Content -LiteralPath $done -Encoding ascii -Value $root
            Log "update $($c.Name) $latest downloaded and unpacked"
        }
        $c.Latest = $latest
        $c.Backup = Join-Path $pending 'backup'
        Set-Status $c "ready $latest"
        return (Get-Content -LiteralPath $done | Select-Object -First 1)
    } catch {
        Log ("$($c.Name) update check failed: " + $_.Exception.Message)   # offline, GitHub down: try again next start
        if ($fresh) { throw }
        return $null
    }
}

# Step 3 for one component, with the game closed. Returns $true when installed.
function Install-Pending($c, $root) {
    $copied = New-Object System.Collections.Generic.List[string]
    try {
        # mk9.exe / the plugin first: if it is still locked nothing else has been touched yet.
        $first = Split-Path -Leaf $c.Marker
        $items = Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object { if ($_.Name -ieq $first) { 0 } else { 1 } }
        foreach ($f in $items) {
            $relPath = $f.FullName.Substring($root.Length).TrimStart('\')
            $target = Join-Path $GameDir $relPath
            if (($c.Keep -contains $relPath) -and (Test-Path -LiteralPath $target)) { continue }   # a first install takes the port's mk9.toml
            if (Test-Path -LiteralPath $target) {
                $b = Join-Path $c.Backup $relPath
                New-Item -ItemType Directory -Path (Split-Path -Parent $b) -Force | Out-Null
                Copy-Item -LiteralPath $target -Destination $b -Force
            } else {
                New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
            }
            $copied.Add($relPath)
            Copy-Item -LiteralPath $f.FullName -Destination $target -Force
        }
    } catch {
        $why = $_.Exception.Message
        foreach ($relPath in $copied) {                               # put the replaced files back
            $b = Join-Path $c.Backup $relPath
            try {
                if (Test-Path -LiteralPath $b) { Copy-Item -LiteralPath $b -Destination (Join-Path $GameDir $relPath) -Force }
                else { Remove-Item -LiteralPath (Join-Path $GameDir $relPath) -Force }
            } catch {}
        }
        if (Test-Path -LiteralPath $c.Backup) { Remove-Item -LiteralPath $c.Backup -Recurse -Force -ErrorAction SilentlyContinue }
        Log "update $($c.Name) $($c.Latest) could not be installed, left as it was: $why"
        Set-Status $c "failed $($c.Latest)"
        return $false
    }
    Remove-Item -LiteralPath (Join-Path $pendingRoot $c.Name) -Recurse -Force -ErrorAction SilentlyContinue
    Set-Status $c "applied $($c.Latest)"
    Log "update $($c.Name) to $($c.Latest) installed ($($copied.Count) files)"
    return $true
}

# One updater at a time: a second game start while one waits just leaves it to the first.
$lock = $null
try {
    $lock = [IO.File]::Open((Join-Path $modDir 'update.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
} catch {
    # first install from launch.cmd: say why instead of closing silently (launch.cmd then tries curl)
    if ($fresh) { Write-Host ('The download could not start: ' + $_.Exception.Message); exit 1 }
    exit 0
}

try {
    # pending\<version> folders from the port-only updater (1.0.5 and older)
    if (Test-Path -LiteralPath $pendingRoot) {
        Get-ChildItem -LiteralPath $pendingRoot -Directory | Where-Object { $_.Name -notin @('port', 'mod') } |
            Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
    if ($fresh) { $port.Installed = '0.0.0'; Say 'MK9 by Lauta is not in this folder yet. Asking GitHub for its latest version.' }
    if (-not $port.Installed) { $port.Installed = Get-LoggedVersion }
    if (-not $mod.Installed) { $mod.Installed = Get-ModDllVersion }

    $todo = @()
    $r = Get-Pending $port; if ($r) { $todo += , @($port, $r) }
    if (-not $fresh) { $r = Get-Pending $mod; if ($r) { $todo += , @($mod, $r) } }
    if (-not $todo.Count) { exit 0 }

    # 3. install once the game has closed
    if ($WaitPid) {
        try { Wait-Process -Id $WaitPid -ErrorAction SilentlyContinue } catch {}
        while (Test-GameRunning) { Start-Sleep -Seconds 2 }
        Start-Sleep -Seconds 1                                    # let Windows release the exe and DLLs
    } elseif (Test-GameRunning) {
        Log 'updates wait: the game is running'; exit 0
    }
    $ok = $true
    foreach ($t in $todo) { if (-not (Install-Pending $t[0] $t[1])) { $ok = $false } }
    if ($fresh) {
        if (-not $ok) { Write-Host 'MK9 by Lauta could not be installed.'; exit 1 }
        Write-Host "MK9 by Lauta $($port.Latest) is installed. Starting the game."
    }
    if (-not $ok) { exit 1 }
} catch {
    Log ('update failed: ' + $_.Exception.Message)
    if ($fresh) { Write-Host ('The download did not work: ' + $_.Exception.Message); exit 1 }
} finally {
    if ($lock) { $lock.Close() }
}
