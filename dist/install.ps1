# MK9 Access: the game files, the first time (run by "MK9 Access launch.cmd" in its console window).
# The port's own installer window (mk9_installer.cpp) has buttons that Tab cannot reach and a progress line a screen
# reader does not announce. So when the folder has no game files yet, this asks in the console instead: it lists the
# Mortal Kombat .iso files it finds (Downloads, Desktop, Documents, this folder and the one above it), or opens a
# Windows Open window, or takes a typed or pasted path. It starts mk9.exe with MK9_TEST_INSTALL set to that path (the
# port's installer then starts copying by itself), brings this console back in front and prints the progress until
# the game files are ready.
# Exit codes: 0 the game files are there (the .cmd starts the game), 3 this script started the game (installing),
# 2 the player quit.
param([string]$GameDir = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$GameDir = [IO.Path]::GetFullPath($GameDir)

function Has-Xex([string]$dir) {
    return (Test-Path -LiteralPath (Join-Path $dir 'default.xex') -PathType Leaf)
}

# The places the port looks (mk9_app.h OnConfigurePaths): mk9.toml's game_data_root, then game_root or
# assets\game_root (or a .nxpack) next to mk9.exe or in any folder above it.
function Game-Present {
    $toml = Join-Path $GameDir 'mk9.toml'
    if (Test-Path -LiteralPath $toml) {
        foreach ($l in [IO.File]::ReadAllLines($toml)) {
            if ($l -match '^\s*game_data_root\s*=\s*"([^"]+)"') {
                $p = $Matches[1] -replace '\\\\', '\'
                if (Has-Xex $p) { return $true }
            }
        }
    }
    $d = $GameDir
    while ($d) {
        foreach ($sub in @('game_root', 'assets\game_root')) {
            if (Has-Xex (Join-Path $d $sub)) { return $true }
            if (Test-Path -LiteralPath ((Join-Path $d $sub) + '.nxpack')) { return $true }
        }
        $up = Split-Path -Parent $d
        if (-not $up -or $up -eq $d) { break }
        $d = $up
    }
    return $false
}

if (Game-Present) { exit 0 }

function Size-Text([long]$bytes) { return ('{0:0.0} GB' -f ($bytes / 1GB)) }

# Mortal Kombat images by name first; otherwise any .iso the size of an Xbox 360 disc. Both at least 6 GB: a PS2 disc
# (Shaolin Monks, Deception) is 4.7 GB at most, a 360 dual-layer image about 7.3.
function Find-Isos {
    $user = $env:USERPROFILE
    $places = @(
        @{ Path = $GameDir; Depth = 1 },
        @{ Path = (Split-Path -Parent $GameDir); Depth = 2 },
        @{ Path = (Join-Path $user 'Downloads'); Depth = 3 },
        @{ Path = [Environment]::GetFolderPath('Desktop'); Depth = 2 },
        @{ Path = [Environment]::GetFolderPath('MyDocuments'); Depth = 2 }
    )
    $seen = @{}
    $all = @()
    foreach ($p in $places) {
        if (-not $p.Path -or -not (Test-Path -LiteralPath $p.Path)) { continue }
        try {
            $files = Get-ChildItem -LiteralPath $p.Path -Filter '*.iso' -File -Recurse -Depth $p.Depth -ErrorAction SilentlyContinue
        } catch { $files = @() }
        foreach ($f in $files) {
            if ($seen.ContainsKey($f.FullName)) { continue }
            $seen[$f.FullName] = $true
            $all += $f
        }
    }
    $named = @($all | Where-Object { $_.Name -match 'mortal|kombat|komplete|mk9' -and $_.Length -ge 6GB })
    if ($named.Count) { return $named }
    return @($all | Where-Object { $_.Length -ge 6.5GB })
}

function Browse {
    Add-Type -AssemblyName System.Windows.Forms
    $owner = New-Object System.Windows.Forms.Form
    $owner.TopMost = $true
    $owner.ShowInTaskbar = $false
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = 'Choose the Mortal Kombat Komplete Edition .iso'
    $dialog.Filter = 'Xbox 360 disc image (*.iso)|*.iso|All files (*.*)|*.*'
    $dialog.InitialDirectory = Join-Path $env:USERPROFILE 'Downloads'
    $result = $dialog.ShowDialog($owner)
    $owner.Dispose()
    if ($result -eq [System.Windows.Forms.DialogResult]::OK) { return $dialog.FileName }
    return $null
}

Write-Host ''
Write-Host 'MK9 Access: the game files are not installed yet.'
Write-Host 'MK9 needs your own copy of Mortal Kombat Komplete Edition for Xbox 360: its .iso file, or a folder where it is already unpacked (the one with default.xex).'
Write-Host 'The files are checked and copied once, about 7 GB, next to the game.'
Write-Host ''

$isos = @(Find-Isos)
$chosen = $null
while (-not $chosen) {
    if ($isos.Count) {
        Write-Host ('Found ' + $isos.Count + $(if ($isos.Count -eq 1) { ' disc image:' } else { ' disc images:' }))
        for ($i = 0; $i -lt $isos.Count; $i++) {
            Write-Host ('' + ($i + 1) + ': ' + $isos[$i].Name + ', ' + (Size-Text $isos[$i].Length) + ', in ' + $isos[$i].DirectoryName)
        }
        Write-Host 'Type its number and press Enter. Or type B and Enter to choose the file in a Windows Open window, paste the full path of the .iso or of the unpacked folder, or type Q to quit.'
    } else {
        Write-Host 'No Mortal Kombat .iso was found in Downloads, the Desktop, Documents or this folder.'
        Write-Host 'Type B and press Enter to choose the file in a Windows Open window, or paste the full path of the .iso or of the unpacked folder and press Enter. Q quits.'
    }
    $answer = (Read-Host 'Your choice').Trim().Trim('"')
    if (-not $answer) { continue }
    if ($answer -match '^[qQ]$') { Write-Host 'Nothing installed.'; exit 2 }
    $n = 0
    if ($isos.Count -and [int]::TryParse($answer, [ref]$n) -and $n -ge 1 -and $n -le $isos.Count) {
        $chosen = $isos[$n - 1].FullName
    } elseif ($answer -match '^[bB]$') {
        Write-Host 'Opening the Open window.'
        $picked = Browse
        if ($picked) { $chosen = $picked } else { Write-Host 'No file chosen.' }
    } elseif (Test-Path -LiteralPath $answer -PathType Leaf) {
        $chosen = [IO.Path]::GetFullPath($answer)
    } elseif (Test-Path -LiteralPath $answer -PathType Container) {
        if (Has-Xex $answer) { $chosen = [IO.Path]::GetFullPath($answer) }
        else { Write-Host 'That folder has no default.xex. Choose the folder that has it, or the .iso.' }
    } else {
        Write-Host 'That was not one of the numbers, and no file or folder has that path.'
    }
    Write-Host ''
}

Write-Host ('Installing from ' + $chosen)
Write-Host 'The port checks the files first and then copies them. This takes a few minutes. Progress is read here; the game starts by itself when it is done.'
$started = Get-Date
$env:MK9_TEST_INSTALL = $chosen
# the same start as the .cmd's: plugins on, saves in the game folder
$gameArgs = @('--mk9_plugins=true', ('--user_data_root="' + (Join-Path $GameDir 'saves') + '"'))
$proc = Start-Process -FilePath (Join-Path $GameDir 'mk9.exe') -ArgumentList $gameArgs -WorkingDirectory $GameDir -PassThru
Remove-Item Env:MK9_TEST_INSTALL

# The port's installer window takes the foreground; bring this console back so the screen reader reads the progress.
Add-Type -Namespace MK9A -Name Win -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
[DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
'@
function Console-Front {
    $me = [MK9A.Win]::GetConsoleWindow()
    if ($me -eq [IntPtr]::Zero) { return }
    $fg = [MK9A.Win]::GetForegroundWindow()
    if ($fg -eq $me) { return }
    $pid0 = 0
    $fgThread = [MK9A.Win]::GetWindowThreadProcessId($fg, [ref]$pid0)
    $myThread = [MK9A.Win]::GetCurrentThreadId()
    [void][MK9A.Win]::AttachThreadInput($myThread, $fgThread, $true)
    [void][MK9A.Win]::ShowWindow($me, 5)
    [void][MK9A.Win]::SetForegroundWindow($me)
    [void][MK9A.Win]::AttachThreadInput($myThread, $fgThread, $false)
}

$logs = Join-Path $GameDir 'logs'
$partial = Join-Path $GameDir 'game_root.partial'
$log = $null
$logPos = 0
$fronted = $false
$lastSay = Get-Date
$lastGb = -1
while ($true) {
    Start-Sleep -Milliseconds 700
    if (-not $fronted -and ((Get-Date) - $started).TotalSeconds -gt 2) { Console-Front; $fronted = $true }
    if (-not $log -and (Test-Path -LiteralPath $logs)) {
        $log = Get-ChildItem -LiteralPath $logs -Filter 'mk9_*.log' -File -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-2) } | Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
    }
    $done = $false; $failed = $null
    if ($log) {
        try {
            $fs = [IO.File]::Open($log.FullName, 'Open', 'Read', 'ReadWrite')
            $fs.Seek($logPos, 'Begin') | Out-Null
            $sr = New-Object IO.StreamReader($fs)
            $new = $sr.ReadToEnd()
            $logPos = $fs.Position
            $sr.Close()
            foreach ($l in ($new -split "`r?`n")) {
                if ($l -notmatch '\[installer\] (.*)$') { continue }
                $what = $Matches[1]
                if ($what -match '^done') { $done = $true }
                elseif ($l -match '\b(warn|warning|error)\b' -and $what -notmatch '^from ') { $failed = $what }
            }
        } catch { }
    }
    if ($done -or (Game-Present)) {
        Write-Host 'Done: the game files are ready. The game is starting; the mod speaks once it has loaded.'
        exit 3
    }
    if ($failed) {
        Write-Host ('The installer stopped: ' + $failed)
        Write-Host 'Close the installer window with Alt+F4 (it is the mk9-lauta Setup window), then start MK9 Access launch.cmd again and choose another file.'
        Write-Host 'Press any key to close this window.'
        [void][Console]::ReadKey($true)
        exit 2
    }
    if ($proc.HasExited) {
        Write-Host 'The game closed before the files were ready. Start MK9 Access launch.cmd again to try once more.'
        Write-Host 'Press any key to close this window.'
        [void][Console]::ReadKey($true)
        exit 2
    }
    if (((Get-Date) - $lastSay).TotalSeconds -ge 15) {
        $lastSay = Get-Date
        if (Test-Path -LiteralPath $partial) {
            $bytes = (Get-ChildItem -LiteralPath $partial -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
            $gb = [math]::Floor($bytes / 1GB * 2) / 2
            if ($gb -ne $lastGb) { Write-Host ('Copied ' + (Size-Text $bytes) + ' of about 7 GB.'); $lastGb = $gb }
        } else {
            Write-Host 'Checking the disc image.'
        }
    }
}
