<#
Prepare a release using static gates only. Default: -DryRun (no tag or publication).
  .\scripts\release.ps1 -Version 1.0 -DryRun
  .\scripts\release.ps1 -Version 1.0 -Publish
Version must match a level-two heading in docs/release/CHANGELOG.md exactly.
#>
[CmdletBinding(DefaultParameterSetName = 'DryRun')]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(\.(0|[1-9][0-9]*))?(-[0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$')]
    [string]$Version,
    [Parameter(ParameterSetName = 'DryRun')]
    [switch]$DryRun,
    [Parameter(Mandatory = $true, ParameterSetName = 'Publish')]
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tag = "v$Version"

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    $global:LASTEXITCODE = 0
    & $Command @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Command failed (exit $LASTEXITCODE)" }
}

function Assert-Clean {
    $status = @(git status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw 'git status failed' }
    if ($status.Count) { throw 'Working tree must be clean, including untracked files. Commit or stash changes first.' }
}

function Quote-Argument([string]$Value) {
    "'" + $Value.Replace("'", "''") + "'"
}

Push-Location $root
try {
    Assert-Clean
    $commit = git rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed' }
    $short = git rev-parse --short HEAD
    if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed' }
    $existing = @(git tag --list $tag)
    if ($LASTEXITCODE -ne 0) { throw 'git tag --list failed' }
    if ($existing.Count) { throw "Tag $tag already exists locally." }

    # Extract before building so an unknown/empty/duplicate version fails early.
    $changelog = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $root 'docs\release\CHANGELOG.md')
    $heading = '(?m)^##[ \t]+\[?' + [regex]::Escape($Version) + '\]?(?:[ \t]+-[ \t]+[^\r\n]+)?[ \t]*\r?$'
    $sections = [regex]::Matches($changelog, $heading)
    if ($sections.Count -ne 1) { throw "Expected exactly one changelog section for $Version." }
    $section = $sections[0]
    $tail = $changelog.Substring($section.Index + $section.Length)
    $next = [regex]::Match($tail, '(?m)^##[ \t]+')
    if ($next.Success) { $tail = $tail.Substring(0, $next.Index) }
    if (-not $tail.Trim()) { throw "Changelog section $Version is empty." }
    $notes = $section.Value.TrimEnd() + $tail.TrimEnd() + "`n"

    Write-Host "Preparing $tag from $commit (static gates)"
    # build.ps1 pins v145 and disables post-build events; package.ps1 uses it too.
    Invoke-Checked (Join-Path $root 'build.ps1') @()
    Invoke-Checked 'uv' @('run', 'pytest', 'tools\tests', '-q')
    Invoke-Checked 'uv' @('run', 'python', 'tools\sitecheck.py')
    Invoke-Checked 'uv' @('run', 'python', 'tools\check_exports.py', 'bin\Release\dinput8.dll')
    Assert-Clean
    Invoke-Checked (Join-Path $PSScriptRoot 'package.ps1') @()
    Assert-Clean

    $zip = Join-Path $root "dist\rs7string-$short.zip"
    $dll = Join-Path $root 'bin\Release\dinput8.dll'
    foreach ($file in @($zip, $dll)) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing release artifact: $file" }
    }
    $sums = Join-Path $root 'dist\SHA256SUMS.txt'
    # Filenames are relative to dist so standard checksum tools can verify both files.
    $hashes = @(
        (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($zip)
        (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant() + '  ../bin/Release/dinput8.dll'
    )
    [IO.File]::WriteAllLines($sums, [string[]]$hashes, [Text.Encoding]::ASCII)
    $notesPath = Join-Path $root "dist\release-notes-$Version.md"
    [IO.File]::WriteAllText($notesPath, $notes, [Text.UTF8Encoding]::new($false))

    $tagArgs = @('tag', '-a', $tag, $commit, '-m', "rs7string $Version")
    $releaseArgs = @('release', 'create', $tag, '--target', $commit, '--title', "rs7string $Version",
                     '--notes-file', $notesPath, $zip, $sums, $notesPath)
    Write-Host 'Prepared release artifacts. Commands (PowerShell, from this repository):'
    Write-Host ('git tag ' + (($tagArgs[1..($tagArgs.Count - 1)] | ForEach-Object { Quote-Argument $_ }) -join ' '))
    Write-Host ('gh release create ' + (($releaseArgs[2..($releaseArgs.Count - 1)] | ForEach-Object { Quote-Argument $_ }) -join ' '))
    if ($Publish) {
        # gh creates the remote tag at this exact commit if it does not already exist.
        # Check the remote explicitly before creating even a local release tag.
        $remote = @(git ls-remote --exit-code --tags origin "refs/tags/$tag")
        $remoteExit = $LASTEXITCODE
        if ($remoteExit -eq 0) { throw "Tag $tag already exists on origin." }
        if ($remoteExit -ne 2) { throw "Cannot check origin tag (exit $remoteExit)." }
        Invoke-Checked 'gh' @('auth', 'status')
        Assert-Clean
        Invoke-Checked 'git' $tagArgs
        Invoke-Checked 'gh' $releaseArgs
    } else {
        Write-Host 'Dry run complete: no tag created and no release published.'
    }
} finally { Pop-Location }
