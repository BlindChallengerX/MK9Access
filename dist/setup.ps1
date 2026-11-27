# MK9 Access: the port settings the mod needs, written into mk9.toml before the game starts
# ("MK9 Access launch.cmd" runs this every time; it only changes what is missing or wrong).
#   always:            mk9_plugins = true (the mod is a code plugin), mk9_update_check = false (the mod updates the port)
#   when missing:      user_data_root = <game>\saves (Controlled Folder Access can crash the game when it saves in
#                      Documents)
#   once per install:  English (user_language = "1", mk9_online_language = "en"), even over a saved Spanish;
#                      MK9Access\english_set.txt marks it done, so a language picked later is kept
#   first start only:  the lightest graphics preset ("Muy baja": smooth play and low input lag) and the port's
#                      first-start steps marked done (language and graphics are set here; the mod speaks the
#                      username window). Detected by mk9_preset_detected missing; later F2 changes are kept.
param([string]$GameDir = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$GameDir = [IO.Path]::GetFullPath($GameDir)
$toml = Join-Path $GameDir 'mk9.toml'
$text = if (Test-Path -LiteralPath $toml) { [IO.File]::ReadAllText($toml) } else { '' }
$lines = New-Object System.Collections.Generic.List[string]
if ($text.Length) { foreach ($l in ($text -split "`r?`n")) { $lines.Add($l) } }
while ($lines.Count -and $lines[$lines.Count - 1] -eq '') { $lines.RemoveAt($lines.Count - 1) }
$changed = $false

function Find-Key([string]$key) {
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match ('^\s*' + [regex]::Escape($key) + '\s*=')) { return $i }
    }
    return -1
}
function Set-Key([string]$key, [string]$value, [string]$comment = '') {
    $i = Find-Key $key
    $line = "$key = $value"
    if ($comment) { $line += "  # $comment" }
    if ($i -lt 0) { $lines.Add($line); $script:changed = $true }
    elseif ($lines[$i] -notmatch ('^\s*' + [regex]::Escape($key) + '\s*=\s*' + [regex]::Escape($value) + '\s*(#.*)?$')) {
        $lines[$i] = $line; $script:changed = $true
    }
}
function Add-Key([string]$key, [string]$value, [string]$comment = '') {
    if ((Find-Key $key) -lt 0) { Set-Key $key $value $comment }
}

Set-Key 'mk9_plugins' 'true' 'MK9 Access: load game\plugins\*.dll'
Set-Key 'mk9_update_check' 'false' 'MK9 Access updates the port itself (MK9Access\mk9update.ps1)'
$saves = (Join-Path $GameDir 'saves') -replace '\\', '/'
Add-Key 'user_data_root' ('"' + $saves + '"') 'MK9 Access: saves in the game folder'
# A game folder moved or renamed after the first start leaves the old "<old folder>/saves" here, and a bare mk9.exe
# start would save there (an empty profile). Same rule as the plugin: a value ending in "saves" whose parent holds no
# mk9.exe is that stale line. A folder the player picked by hand is kept.
$ri = Find-Key 'user_data_root'
if ($ri -ge 0 -and $lines[$ri] -match '^\s*user_data_root\s*=\s*["'']([^"'']+)["'']') {
    $old = $Matches[1].TrimEnd('/', '\')
    $parent = Split-Path -Parent $old
    if ((Split-Path -Leaf $old) -ieq 'saves' -and $parent -and
        ([IO.Path]::GetFullPath($parent).TrimEnd('\') -ine $GameDir.TrimEnd('\')) -and
        -not (Test-Path -LiteralPath (Join-Path $parent 'mk9.exe'))) {
        Set-Key 'user_data_root' ('"' + $saves + '"') 'MK9 Access: saves in the game folder'
    }
}
# English: set once per install, whatever was there (the port's own default is Spanish, and its first-start window
# saves Spanish when the player just accepts it). MK9Access\english_set.txt records it, so a language chosen
# afterwards (F2 or mk9.toml) is kept.
$englishMark = Join-Path $GameDir 'MK9Access\english_set.txt'
$setEnglish = -not (Test-Path -LiteralPath $englishMark)
if ($setEnglish) {
    Set-Key 'user_language' '"1"' 'MK9 Access: English (5 Spanish, 9 Portuguese; a change here is kept)'
    Set-Key 'mk9_online_language' '"en"'
} else {
    Add-Key 'user_language' '"1"' 'MK9 Access: English'
    Add-Key 'mk9_online_language' '"en"'
}
if ((Find-Key 'mk9_preset_detected') -lt 0) {
    Set-Key 'renut_resolution' '"480p"'
    Set-Key 'renut_shadow_resolution' '"1x"'
    Set-Key 'renut_ambient_occlusion' '"Half"'
    Set-Key 'renut_anisotropic_filtering' '"Game"'
    Set-Key 'renut_post_process_precision' '"Game"'
    Set-Key 'mk9_draw_mode' '"auto"'
    Set-Key 'mk9_dynamic_resolution' 'false'
    Set-Key 'mk9_preset_detected' '"Muy baja"' 'MK9 Access: the lightest graphics (F2 changes them)'
    Set-Key 'mk9_first_run_done' 'true'
}
if ($changed) {
    [IO.File]::WriteAllText($toml, (($lines -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
}
if ($setEnglish -and (Test-Path -LiteralPath (Split-Path -Parent $englishMark))) {
    [IO.File]::WriteAllText($englishMark, "MK9 Access set English in mk9.toml on $(Get-Date -Format 'yyyy-MM-dd'). Delete this file to have it set English again.`r`n")
}
