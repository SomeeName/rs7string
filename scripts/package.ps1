<#
    Release package for rs7string. Builds Release (build.ps1), then writes
    dist\rs7string-<git short hash>.zip (suffix -dirty when the tree has uncommitted changes):
      bin/Release/dinput8.dll
      bin/Release/chord-diagrams/...      (if present)
      bin/Release/rs7string_headstock.bin, bin/Release/rs7string_headstock_lh.bin, bin/Release/rs7string_headstock.LICENSE.md
                                          (headstock data, right- and left-handed, and its MIT licence)
      bin/Release/rs7string.ini           template generated from the defaults, one comment line per key
      docs/release/*.md
    Only these inputs are read. Nothing from the game folder, original\, Desktop\rocksmith-assets or
    any .psarc/.nif/.dds/.gfx file may be in the zip: the finished zip is re-opened and checked, and
    deleted again if a check fails.
      .\scripts\package.ps1
#>
param([switch]$TemplateOnly)   # print the generated rs7string.ini and stop (no build, no zip)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = Split-Path -Parent $PSScriptRoot
$tablesCpp = Join-Path $root 'src\settings_tables.cpp'   # kRs7SettingTables: every feature's table, in order
# The template's sections; inside a section the keys keep the table order of kRs7SettingTables.
$sectionOrder = 'overlay', 'debug', 'color', 'neck', 'assets', 'chords', 'tuner', 'psarc', 'window', 'test'

# Keys the code reads outside the settings table (no table row, so the default and comment are here).
# Each key must still appear in the sources (checked below), so a renamed key fails the package.
$extraRows = @(
    [pscustomobject]@{ sec = 'chords'; key = 'wide_misses'; def = '2'; desc = 'Notes that may be missing when matching 7- and 8-note chord requests, on any string (0-2). 5- and 6-note chords allow one miss; 2-4-note chords allow none, as in stock.' }
    [pscustomobject]@{ sec = 'chords'; key = 'frame_scale'; def = '1.134'; desc = 'No longer used: the chord frame now follows the guitar''s lane positions (stock frame for chords with strings 0-5, one lane more per added string). Old values are read and ignored.' }
    [pscustomobject]@{ sec = 'chords'; key = 'frame_axis'; def = '1'; desc = 'Axis (0-2) of the chord frame span. Invalid values fall back to 1.' }
    [pscustomobject]@{ sec = 'chords'; key = 'diagrams7'; def = '1'; desc = 'Seven-row chord diagrams (issue #20): 1 (default) writes the native chord-diagram data and swaps in the movies from chord-diagrams, hash-checked against the September 2022 build (any other build keeps the stock diagrams); 0 writes none.' }
    [pscustomobject]@{ sec = 'debug'; key = 'veh_log'; def = '0'; desc = 'Diagnostic: logs the first 40 exceptions through a vectored exception handler. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'alloc_stats'; def = '0'; desc = 'Diagnostic: logs the DLL live/total allocation counts at song start and end. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'diagrams_trace'; def = '0'; desc = 'Diagnostic: logs chord-diagram recipe loading and movie swaps. Only used with [chords] diagrams7=1. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'spin_probe'; def = '0'; desc = 'Diagnostic: starts a watchdog that samples busy threads (suspends and resumes them briefly). Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'plain_delay_ms'; def = '0'; desc = 'Test only: starts the plain-PSARC patches this many ms late (at most 30000), so an enabler with its own plain-PSARC support patches first. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'log_redact'; def = '1'; desc = 'rs7string.log hides your Windows user name, user folder and computer name. 0 writes them as they are (own debugging only, never post such a log). Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'publish_trace'; def = '0'; desc = 'Diagnostic: logs the GridBuild readiness waits of the native layouts (a guitar built with 7/8 strings). Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'skip_sites'; def = ''; desc = 'Test bisection: comma-separated site IDs that are resolved but not written at startup. Leave empty.' }
    [pscustomobject]@{ sec = 'assets'; key = 'selfbuild'; def = '1'; desc = '1 = build the 7-string headstock model from your own game files at the first guitar load (once; cached in dir). Stock headstock on any failure.' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar'; def = '0'; desc = 'Test only: enables the virtual guitarist, which synthesises chart notes into the audio input path.' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_mode'; def = 'chart'; desc = 'Pitch source: chart, game, midi:N (MIDI note 20-100) or off.' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_filter'; def = 'all'; desc = 'Notes to play: all, strings such as 6 or 056, or string:fret such as 6:0 (fret 0-30).' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_lead_ms'; def = '0'; desc = 'Onset advance in milliseconds (-500 to 500); a positive value sends the notes earlier.' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_gain'; def = '0.3'; desc = 'Level of the synthesised pluck, 0-1.' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_mix'; def = '0'; desc = '0 replaces the real input during a song; 1 adds the synthesised notes to it.' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_trace'; def = '0'; desc = 'Logs the detector frame after each synthesised onset (diagnostic).' }
    [pscustomobject]@{ sec = 'test'; key = 'vguitar_chord6'; def = 'play'; desc = 'Seventh-string note of a chord: play, omit, only, +N or -N (semitones), omitlow or omithigh.' }
    [pscustomobject]@{ sec = 'test'; key = 'fast'; def = '0'; desc = 'Test only: a song ends on its song page (no results screen).' }
    [pscustomobject]@{ sec = 'test'; key = 'force_colourblind'; def = '0'; desc = 'Test only: colourblind mode for this session; the profile keeps its setting.' }
    [pscustomobject]@{ sec = 'test'; key = 'force_lefty'; def = '0'; desc = 'Test only: left-handed guitar for this session; the profile keeps its setting.' }
    [pscustomobject]@{ sec = 'test'; key = 'force_headstock'; def = ''; desc = 'Test only: headstock type for this session, 33 (3+3) or 6 (6 in-line); empty: the profile''s.' }
)

function Get-CStringValue([string]$raw) {
    # Undo the two C escapes the table uses: \\ and \".
    $sb = [System.Text.StringBuilder]::new()
    for ($i = 0; $i -lt $raw.Length; $i++) {
        $c = $raw[$i]
        if ($c -eq '\' -and $i + 1 -lt $raw.Length) { $i++; $c = $raw[$i] }
        [void]$sb.Append($c)
    }
    $sb.ToString()
}

function Get-SettingsBlocks {
    # The Rs7SetDef array of every table listed in kRs7SettingTables, in list order (a table without rows has none).
    $list = Get-Content -Raw -LiteralPath $tablesCpp
    $start = $list.IndexOf('kRs7SettingTables[] = {')
    if ($start -lt 0) { throw "kRs7SettingTables not found in $tablesCpp" }
    $names = [regex]::Matches($list.Substring($start, $list.IndexOf('};', $start) - $start), '&(kRs7\w+Settings)') | ForEach-Object { $_.Groups[1].Value }
    $files = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File -Filter *.cpp
    foreach ($name in $names) {
        $defRx = [regex]('const Rs7SetTable ' + $name + ' = \{(\w+),')
        $hits = @($files | ForEach-Object { $t = Get-Content -Raw -LiteralPath $_.FullName; $m = $defRx.Match($t); if ($m.Success) { [pscustomobject]@{ text = $t; arr = $m.Groups[1].Value } } })
        if ($hits.Count -ne 1) { throw "settings table ${name}: defined $($hits.Count) times in src/" }
        if ($hits[0].arr -eq 'nullptr') { continue }
        $a = $hits[0].text.IndexOf('const Rs7SetDef ' + $hits[0].arr + '[] = {')
        $b = $hits[0].text.IndexOf("`n};", [Math]::Max($a, 0))
        if ($a -lt 0 -or $b -lt 0) { throw "rows $($hits[0].arr) of $name not found" }
        $hits[0].text.Substring($a, $b - $a)
    }
}

function Read-SettingsTable {
    foreach ($block in Get-SettingsBlocks) { Read-SettingsBlock $block }
}

function Read-SettingsBlock([string]$block) {
    $entryRx = [regex]'\{"(?<id>[a-z0-9_.]+)",\s*L"(?<sec>\w+)",\s*L"(?<key>\w+)",\s*T::\w+,\s*W::\w+,\s*"(?<def>(?:[^"\\]|\\.)*)",[^"]*"(?:[^"\\]|\\.)*",\s*(?<desc>(?:"(?:[^"\\]|\\.)*"\s*)+)'
    $found = $entryRx.Matches($block)
    $declared = ([regex]::Matches($block, '\{"[a-z0-9_.]+"')).Count
    if ($found.Count -eq 0 -or $found.Count -ne $declared) {
        throw "could not parse the settings table: $($found.Count) of $declared rows"
    }
    foreach ($m in $found) {
        $pieces = [regex]::Matches($m.Groups['desc'].Value, '"((?:[^"\\]|\\.)*)"') | ForEach-Object { Get-CStringValue $_.Groups[1].Value }
        [pscustomobject]@{
            sec = $m.Groups['sec'].Value
            key = $m.Groups['key'].Value
            def = Get-CStringValue $m.Groups['def'].Value
            desc = ($pieces -join '')
        }
    }
}

function Get-SourceText {
    $files = Get-ChildItem -LiteralPath (Join-Path $root 'src') -Recurse -File -Include *.cpp, *.h
    ($files | ForEach-Object { Get-Content -Raw -LiteralPath $_.FullName }) -join "`n"
}

function New-IniTemplate([string]$commit) {
    $rows = @(Read-SettingsTable)
    $src = Get-SourceText
    foreach ($x in $extraRows) {
        if (-not $src.Contains('"' + $x.key + '"') -or -not $src.Contains('"' + $x.sec + '"')) {
            throw "extra key [$($x.sec)] $($x.key) is no longer read by src/; update scripts/package.ps1"
        }
    }
    $all = @($rows) + @($extraRows)
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('; rs7string settings. Put this file beside dinput8.dll. Missing keys use the defaults.')
    $lines.Add("; Generated by scripts/package.ps1 for commit $commit from the settings tables in src/settings_tables.cpp")
    $lines.Add('; and the keys read elsewhere in src/. 0 = off, 1 = on. SETTINGS.md explains each key in plain words.')
    $placed = 0
    foreach ($s in $sectionOrder) {
        $items = @($all | Where-Object { $_.sec -eq $s })
        if ($items.Count -eq 0) { continue }
        $lines.Add('')
        $lines.Add("[$s]")
        foreach ($r in $items) {
            $placed++
            $id = "$($r.sec).$($r.key)"
            # Owner-specific absolute paths and the colourblind colour (which follows string6_color) are
            # written commented out, so the shipped template never carries a developer path.
            $placeholder = ($r.def -match '^[A-Za-z]:\\') -or ($id -eq 'color.string6_color_cb')
            if ($placeholder) {
                $note = if ($r.def -match '^[A-Za-z]:\\') { 'No portable default is shipped: set your own path, or leave this line commented.' }
                        else { 'Leave this line commented to follow string6_color.' }
                $lines.Add("; $($r.desc) $note")
                $lines.Add(";$($r.key)=")
            } else {
                $lines.Add("; $($r.desc)")
                $lines.Add("$($r.key)=$($r.def)")
            }
        }
    }
    if ($placed -ne $all.Count) { throw "template placed $placed of $($all.Count) keys" }
    $text = ($lines -join "`r`n") + "`r`n"
    if ($text -match '[^\x09\x0A\x0D\x20-\x7E]') { throw 'template contains non-ASCII text' }
    if ($text -match '[A-Za-z]:\\') { throw 'template contains a drive-letter path' }
    [pscustomobject]@{ text = $text; keys = $placed }
}

function Add-ZipFile($zip, [string]$path, [string]$entry) {
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $path, $entry, [System.IO.Compression.CompressionLevel]::Optimal)
}

function Test-ZipContents([string]$zipPath) {
    $allow = '^(bin/Release/(dinput8\.dll|rs7string\.ini|rs7string_headstock(_lh)?\.bin|rs7string_headstock\.LICENSE\.md|chord-diagrams/.+)|docs/release/[^/]+\.md)$'
    $deny = '(?i)\.(psarc|nif|dds|gfx|swf|sng|bnk|wem|ogg)$|(^|/)(original|rskeys|steamapps|rocksmith-assets|Rocksmith2014)(/|$)|rocksmith2014'
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName })
    } finally { $zip.Dispose() }
    $problems = @()
    foreach ($n in $names) {
        if ($n -match $deny) { $problems += "FORBIDDEN (game/original/asset file): $n" }
        elseif ($n -notmatch $allow) { $problems += "not in the release allowlist: $n" }
    }
    if (@($names | Group-Object | Where-Object Count -gt 1).Count) { $problems += 'duplicate entry names' }
    foreach ($req in 'bin/Release/dinput8.dll', 'bin/Release/rs7string.ini', 'bin/Release/rs7string_headstock.bin', 'bin/Release/rs7string_headstock_lh.bin') {
        if ($names -notcontains $req) { $problems += "missing: $req" }
    }
    if (-not ($names | Where-Object { $_ -like 'docs/release/*.md' })) { $problems += 'missing: docs/release/*.md' }
    if ($problems.Count) { throw ("zip check failed:`n  " + ($problems -join "`n  ")) }
    $names
}

Push-Location $root
try {
    $commit = (git -C $root rev-parse --short HEAD).Trim()
    if ($LASTEXITCODE -or -not $commit) { throw 'git rev-parse failed' }
    $template = New-IniTemplate $commit   # fails early, before the long build, if the table moved
    if ($TemplateOnly) { $template.text; return }

    & "$root\build.ps1"
    if ($LASTEXITCODE) { throw 'build.ps1 failed' }

    $dll = Join-Path $root 'bin\Release\dinput8.dll'
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw "missing $dll after the build" }
    $headstock = 'rs7string_headstock.bin', 'rs7string_headstock_lh.bin', 'rs7string_headstock.LICENSE.md'
    foreach ($h in $headstock) {
        if (-not (Test-Path -LiteralPath (Join-Path $root "bin\Release\$h") -PathType Leaf)) { throw "missing bin\Release\$h after the build" }
    }

    $dirty = [bool](git -C $root status --porcelain)
    if ($dirty) { Write-Warning 'uncommitted changes: the zip is named -dirty and does not match a commit' }
    $name = "rs7string-$commit" + $(if ($dirty) { '-dirty' } else { '' }) + '.zip'

    $dist = Join-Path $root 'dist'
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    $zipPath = Join-Path $dist $name
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }

    $zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        Add-ZipFile $zip $dll 'bin/Release/dinput8.dll'
        foreach ($h in $headstock) { Add-ZipFile $zip (Join-Path $root "bin\Release\$h") "bin/Release/$h" }

        $cd = Join-Path $root 'bin\Release\chord-diagrams'
        if (Test-Path -LiteralPath $cd -PathType Container) {
            foreach ($f in Get-ChildItem -LiteralPath $cd -Recurse -File) {
                $rel = $f.FullName.Substring($cd.Length).TrimStart('\', '/').Replace('\', '/')
                Add-ZipFile $zip $f.FullName "bin/Release/chord-diagrams/$rel"
            }
        } else { Write-Warning 'bin\Release\chord-diagrams not present: skipped' }

        $entry = $zip.CreateEntry('bin/Release/rs7string.ini', [System.IO.Compression.CompressionLevel]::Optimal)
        $stream = $entry.Open()
        try {
            $bytes = [System.Text.Encoding]::ASCII.GetBytes($template.text)
            $stream.Write($bytes, 0, $bytes.Length)
        } finally { $stream.Dispose() }

        $docs = Get-ChildItem -LiteralPath (Join-Path $root 'docs\release') -Filter '*.md' -File | Sort-Object Name
        foreach ($d in $docs) { Add-ZipFile $zip $d.FullName "docs/release/$($d.Name)" }
    } finally { $zip.Dispose() }

    try {
        $names = Test-ZipContents $zipPath
    } catch {
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
        throw
    }

    Write-Host "Template: $($template.keys) keys"
    Write-Host "Wrote $zipPath"
    $z = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $z.Entries | ForEach-Object { '{0,10}  {1}' -f $_.Length, $_.FullName }
    } finally { $z.Dispose() }
} finally { Pop-Location }
