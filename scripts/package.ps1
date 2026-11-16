<#
    Release package for rs7string. Builds Release (build.ps1), then writes
    dist\rs7string-<git short hash>.zip (suffix -dirty when the tree has uncommitted changes):
      bin/Release/dinput8.dll
      bin/Release/chord-diagrams/...      (from data/chord-diagrams)
      bin/Release/rs7string_headstock[_lh|_bass|_bass_lh].bin, bin/Release/rs7string_headstock.LICENSE.md
                                          (headstock data, guitar and bass, right- and left-handed, and its MIT licence)
      bin/Release/rs7string.ini           template generated from the defaults, one comment line per key
      docs/release/*.md, docs/release/img/*.png|*.html (the chart graphic the guides link to)
    Only these inputs are read. Nothing from the game folder, original\, Desktop\rocksmith-assets or
    any .psarc/.nif/.dds/.gfx file may be in the zip: the finished zip is re-opened and checked, and
    deleted again if a check fails.
      .\scripts\package.ps1
#>
param(
    [switch]$TemplateOnly,   # print the generated rs7string.ini and stop (no build, no zip)
    [string]$ReleaseDoc      # print one docs/release file as the zip carries it (links rewritten) and stop
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$root = Split-Path -Parent $PSScriptRoot
$tablesCpp = Join-Path $root 'src\settings_tables.cpp'   # kRs7SettingTables: every feature's table, in order
$chordDiagramNames = 'commonlib.rs7da', 'contentpanel.rs7da', 'ge_game.rs7da', 'LICENSE', 'README.md'
# The template's sections; inside a section the keys keep the table order of kRs7SettingTables.
$sectionOrder = 'overlay', 'debug', 'color', 'neck', 'assets', 'chords', 'tuner', 'psarc', 'window', 'fixes', 'test'

# Keys the code reads outside the settings table (no table row, so the default and comment are here).
# Each key must still appear in the sources (checked below), so a renamed key fails the package.
$extraRows = @(
    [pscustomobject]@{ sec = 'chords'; key = 'diagrams7'; def = '1'; desc = 'Chord diagrams with rows for the seventh and eighth string: 1 (default) writes the native chord-diagram data and swaps in the movies from chord-diagrams, hash-checked against the September 2022 build (any other build keeps the stock diagrams); 0 writes none.' }
    [pscustomobject]@{ sec = 'debug'; key = 'veh_log'; def = '0'; desc = 'Diagnostic: logs the first 40 exceptions through a vectored exception handler. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'alloc_stats'; def = '0'; desc = 'Diagnostic: logs the DLL live/total allocation counts at song start and end. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'diagrams_trace'; def = '0'; desc = 'Diagnostic: logs chord-diagram recipe loading and movie swaps. Only used with [chords] diagrams7=1. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'spin_probe'; def = '0'; desc = 'Diagnostic: starts a watchdog that samples busy threads (suspends and resumes them briefly). Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'plain_delay_ms'; def = '0'; desc = 'Test only: starts the plain-PSARC patches this many ms late (at most 30000), so an enabler with its own plain-PSARC support patches first. Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'log_redact'; def = '1'; desc = 'rs7string.log hides your Windows user name, user folder and computer name. 0 writes them as they are (own debugging only, never post such a log). Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'publish_trace'; def = '0'; desc = 'Diagnostic: logs the GridBuild readiness waits of the native layouts (a guitar built with 7/8 strings). Read at startup.' }
    [pscustomobject]@{ sec = 'debug'; key = 'skip_sites'; def = ''; desc = 'Test bisection: comma-separated site IDs that are resolved but not written at startup. Leave empty.' }
    [pscustomobject]@{ sec = 'assets'; key = 'selfbuild'; def = '1'; desc = '1 = build the 7-string headstock model from your own game files at the first guitar load (once; cached in dir). Stock headstock on any failure.' }
    [pscustomobject]@{ sec = 'test'; key = 'fast'; def = '0'; desc = 'Test only: a song ends on its song page (no results screen).' }
    [pscustomobject]@{ sec = 'test'; key = 'force_colourblind'; def = '0'; desc = 'Test only: colourblind mode for this session; the profile keeps its setting.' }
    [pscustomobject]@{ sec = 'test'; key = 'force_lefty'; def = '0'; desc = 'Test only: left-handed guitar for this session; the profile keeps its setting.' }
    [pscustomobject]@{ sec = 'test'; key = 'force_headstock'; def = ''; desc = 'Test only: headstock type for this session, 33 (3+3) or 6 (6 in-line); empty: the profile''s.' }
)

# Developer keys: they still work, but the public template leaves them out (docs/DEVELOPMENT.md lists them).
$devOnly = @(
    'debug.tuner_autotune', 'debug.head_cull', 'debug.veh_log',
    'debug.skip_sites', 'debug.alloc_stats', 'debug.diagrams_trace', 'debug.spin_probe', 'debug.plain_delay_ms',
    'debug.publish_trace', 'window.background', 'test.fast', 'test.force_colourblind', 'test.force_lefty',
    'test.force_headstock'
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
    $known = @(@($rows) + @($extraRows) | ForEach-Object { "$($_.sec).$($_.key)" })
    foreach ($d in $devOnly) {
        if ($known -notcontains $d) { throw "developer key $d is no longer a settings row; update `$devOnly in scripts/package.ps1" }
    }
    $all = @(@($rows) + @($extraRows) | Where-Object { $devOnly -notcontains "$($_.sec).$($_.key)" })
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

# The release zip carries only docs/release. Links from there to other repository docs (README, developer guides)
# would be dead in the zip, so they point to the same page on the published docs site (mkdocs.yml site_url).
function Get-SiteUrl {
    $line = Get-Content -LiteralPath (Join-Path $root 'mkdocs.yml') | Where-Object { $_ -match '^site_url:\s*(\S+)' } | Select-Object -First 1
    if (-not $line) { throw 'mkdocs.yml has no site_url' }
    $url = ($line -replace '^site_url:\s*', '').Trim()
    if (-not $url.EndsWith('/')) { $url += '/' }
    $url
}

function Convert-ReleaseLinks([string]$text, [string]$site) {
    [regex]::Replace($text, '\]\((\.\./[^)#\s]+?\.md)(#[^)\s]*)?\)', {
        param($m)
        $parts = [System.Collections.Generic.List[string]]::new()
        foreach ($s in ('docs/release/' + $m.Groups[1].Value).Split('/')) {
            if ($s -eq '..') { $parts.RemoveAt($parts.Count - 1) } elseif ($s -and $s -ne '.') { $parts.Add($s) }
        }
        $path = $parts -join '/'
        $page = if ($path -eq 'README.md') { '' } else { $path.Substring(0, $path.Length - 3) + '/' }
        '](' + $site + $page + $m.Groups[2].Value + ')'
    })
}

function Add-ZipText($zip, [string]$text, [string]$entry) {
    $stream = $zip.CreateEntry($entry, [System.IO.Compression.CompressionLevel]::Optimal).Open()
    try {
        $bytes = (New-Object System.Text.UTF8Encoding($false)).GetBytes($text)
        $stream.Write($bytes, 0, $bytes.Length)
    } finally { $stream.Dispose() }
}

function Add-ZipFile($zip, [string]$path, [string]$entry) {
    [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $path, $entry, [System.IO.Compression.CompressionLevel]::Optimal)
}

function Get-ChordDiagramFiles {
    $cd = Join-Path $root 'data\chord-diagrams'
    if (-not (Test-Path -LiteralPath $cd -PathType Container)) { throw "missing $cd" }
    $files = @(Get-ChildItem -LiteralPath $cd -Recurse -File -Force)
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($cd.Length).TrimStart('\', '/').Replace('\', '/')
        if ($chordDiagramNames -cnotcontains $rel) { throw "unexpected chord-diagram file: $rel" }
    }
    foreach ($name in $chordDiagramNames) {
        if (-not (Test-Path -LiteralPath (Join-Path $cd $name) -PathType Leaf)) { throw "missing chord-diagram file: $name" }
    }
    $files
}

function Test-ZipContents([string]$zipPath) {
    $diagramAllow = ($chordDiagramNames | ForEach-Object { [regex]::Escape($_) }) -join '|'
    $allow = '^(bin/Release/(dinput8\.dll|rs7string\.ini|rs7string_headstock(_bass)?(_lh)?\.bin|rs7string_headstock\.LICENSE\.md|chord-diagrams/(' + $diagramAllow + '))|docs/release/[^/]+\.md|docs/release/img/[^/]+\.(png|html))$'
    $deny = '(?i)\.(psarc|nif|dds|gfx|swf|sng|bnk|wem|ogg)$|(^|/)(original|rskeys|steamapps|rocksmith-assets|Rocksmith2014)(/|$)|rocksmith2014'
    # The private-only features (.publicignore): neither a file name nor any text in the zip may mention them. Their
    # names are in scripts/private-words.txt, itself private-only; without it (the public tree) nothing private exists.
    $wordsFile = Join-Path $PSScriptRoot 'private-words.txt'
    $private = if (Test-Path $wordsFile) { '(?i)' + ((Get-Content $wordsFile | Where-Object { $_ -and $_ -notmatch '^#' }) -join '|') } else { '(?!)' }
    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    $problems = @()
    try {
        $names = @($zip.Entries | ForEach-Object { $_.FullName })
        foreach ($e in $zip.Entries) {
            if ($e.FullName -notmatch '\.(md|html|ini)$') { continue }
            $reader = [System.IO.StreamReader]::new($e.Open(), [System.Text.Encoding]::UTF8)
            try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
            if ($text -match $private) { $problems += "PRIVATE feature mentioned in: $($e.FullName)" }
        }
    } finally { $zip.Dispose() }
    foreach ($n in $names) {
        if ($n -match $private) { $problems += "FORBIDDEN (private feature): $n" }
        elseif ($n -match $deny) { $problems += "FORBIDDEN (game/original/asset file): $n" }
        elseif ($n -notmatch $allow) { $problems += "not in the release allowlist: $n" }
    }
    if (@($names | Group-Object | Where-Object Count -gt 1).Count) { $problems += 'duplicate entry names' }
    foreach ($req in 'bin/Release/dinput8.dll', 'bin/Release/rs7string.ini', 'bin/Release/rs7string_headstock.bin', 'bin/Release/rs7string_headstock_lh.bin',
        'bin/Release/rs7string_headstock_bass.bin', 'bin/Release/rs7string_headstock_bass_lh.bin') {
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
    if ($ReleaseDoc) {
        [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
        Convert-ReleaseLinks ([IO.File]::ReadAllText((Join-Path $root $ReleaseDoc))) (Get-SiteUrl)
        return
    }

    $chordDiagrams = @(Get-ChordDiagramFiles)   # reject stray inputs before building or creating an archive
    & "$root\build.ps1" -Public   # the shipped DLL leaves out the private extras (src/features/extras/extras.h)
    if ($LASTEXITCODE) { throw 'build.ps1 failed' }

    $dll = Join-Path $root 'bin\Release\dinput8.dll'
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { throw "missing $dll after the build" }
    & uv run python "$root\tools\check_public_dll.py" $dll   # release guard: no private-only strings in the shipped DLL
    if ($LASTEXITCODE) { throw 'check_public_dll.py failed: the DLL contains private code' }
    $headstock = 'rs7string_headstock.bin', 'rs7string_headstock_lh.bin', 'rs7string_headstock_bass.bin',
        'rs7string_headstock_bass_lh.bin', 'rs7string_headstock.LICENSE.md'
    foreach ($h in $headstock) {
        if (-not (Test-Path -LiteralPath (Join-Path $root "bin\Release\$h") -PathType Leaf)) { throw "missing bin\Release\$h after the build" }
    }

    $status = @(git -C $root status --porcelain)
    if ($LASTEXITCODE) { throw 'git status failed' }
    $dirty = $status.Count -gt 0
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

        # From data\chord-diagrams (what build.ps1 copies), not bin\Release: that copy never removes old files,
        # so a renamed or deleted file would still ship.
        foreach ($f in $chordDiagrams) {
            Add-ZipFile $zip $f.FullName "bin/Release/chord-diagrams/$($f.Name)"
        }

        Add-ZipText $zip $template.text 'bin/Release/rs7string.ini'   # ASCII only (New-IniTemplate checks), so UTF-8 = ASCII bytes

        $docs = Get-ChildItem -LiteralPath (Join-Path $root 'docs\release') -Filter '*.md' -File | Sort-Object Name
        $site = Get-SiteUrl
        foreach ($d in $docs) { Add-ZipText $zip (Convert-ReleaseLinks ([IO.File]::ReadAllText($d.FullName)) $site) "docs/release/$($d.Name)" }
        $img = Join-Path $root 'docs\release\img'
        if (Test-Path -LiteralPath $img) {
            $pics = Get-ChildItem -LiteralPath $img -File | Where-Object { $_.Extension -in '.png', '.html' } | Sort-Object Name
            foreach ($d in $pics) { Add-ZipFile $zip $d.FullName "docs/release/img/$($d.Name)" }
        }
    } finally { $zip.Dispose() }

    try {
        [void](Test-ZipContents $zipPath)
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
