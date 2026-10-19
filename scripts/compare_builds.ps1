<#
    Prove that a source change (formatting, braces, blank lines, a refactor) left the machine code alone:
    build two revisions with a linker map and compare every function of dinput8.dll and rs7string_tests.exe
    (tools\cmp_functions.py: symbol names, not addresses, so moved code compares equal).
      .\scripts\compare_builds.ps1 -Old 66d3def -New HEAD            functions
      .\scripts\compare_builds.ps1 -Old 66d3def -New HEAD -Data      functions and the data symbols of the DLL
      .\scripts\compare_builds.ps1 -Old main -New HEAD -NoOpt        for moving or splitting source files
    -NoOpt builds both revisions with /Od /Ob0 (no optimisation, no inlining; via the _CL_ environment
    variable, no project edit) into separate cache folders (<short sha>-noopt). Use it when files are moved or
    split: in the Release build that changes inlining and frame layout, so functions differ although no source
    line changed. At /Od a function's code depends only on its own body, so moved code compares function by
    function. The comparison tool also normalises what MSVC derives from the source path (anonymous-namespace
    hashes, string-literal and other compiler-generated symbol names). The result proves the code is the same;
    it is no statement about the optimised build.
    -Alias <namespace>[,...] counts those namespaces as the anonymous one (code that moved out of an anonymous
    namespace into a named one, e.g. -Alias rs7asset). The anonymous namespace itself is never compared.
    Both revisions are built in the same folder (..\rs7-cmp-work): MSVC derives the names of anonymous-namespace
    and lambda symbols from the source path. Builds are cached in ..\rs7-cmp-out\<short sha>; delete a folder
    there to rebuild. Nothing is copied into a game folder.
#>
param(
    [Parameter(Mandatory)][string]$Old,
    [Parameter(Mandatory)][string]$New,
    [switch]$Data,
    [switch]$NoOpt,
    [string[]]$Alias = @()
)
$ErrorActionPreference = 'Stop'
$repo = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim().Replace('/', '\')
$work = Join-Path (Split-Path $repo) 'rs7-cmp-work'
$out = Join-Path (Split-Path $repo) 'rs7-cmp-out'
$suffix = if ($NoOpt) { '-noopt' } else { '' }

function Build-Rev([string]$Rev) {
    $sha = (git -C $repo rev-parse --short=10 $Rev).Trim()
    $dest = Join-Path $out ($sha + $suffix)
    if (Test-Path (Join-Path $dest 'rs7string_tests.map')) { return $dest }
    if (Test-Path $work) { git -C $repo worktree remove --force $work | Out-Null; if (Test-Path $work) { Remove-Item -Recurse -Force $work } }
    git -C $repo worktree add --detach $work $sha | Out-Null
    $env:_LINK_ = '/MAP'    # link.exe writes <output>.map; no project or source change
    if ($NoOpt) { $env:_CL_ = '/Od /Ob0' }    # cl.exe appends _CL_: overrides the project's /O2 (warning D9025)
    try {
        # build.ps1 throws when a step fails; its output must not become part of this function's result
        & (Join-Path $work 'build.ps1') -SkipFormatCheck -NoTests | Out-Host
    } finally { Remove-Item Env:\_LINK_, Env:\_CL_ -ErrorAction SilentlyContinue }
    New-Item -ItemType Directory -Force $dest | Out-Null
    foreach ($f in 'dinput8.dll', 'dinput8.map', 'rs7string_tests.exe', 'rs7string_tests.map') {
        Copy-Item -LiteralPath (Join-Path $work "bin\Release\$f") -Destination $dest
    }
    git -C $repo worktree remove --force $work | Out-Null
    return $dest
}

$a = Build-Rev $Old
$b = Build-Rev $New
$rc = 0
Push-Location $repo
try {
    foreach ($pair in @(@('dinput8.dll', 'dinput8.map'), @('rs7string_tests.exe', 'rs7string_tests.map'))) {
        Write-Host "== $($pair[0])  ($Old -> $New$(if ($NoOpt) { ', -NoOpt' }))"
        $extra = @(); if ($Data -and $pair[0] -eq 'dinput8.dll') { $extra = @('--data') }
        # `powershell -File` passes -Alias a,b as one string: split it here too.
        foreach ($ns in ($Alias -split ',' | Where-Object { $_ })) { $extra += @('--alias', $ns) }
        uv run python tools/cmp_functions.py (Join-Path $a $pair[0]) (Join-Path $a $pair[1]) (Join-Path $b $pair[0]) (Join-Path $b $pair[1]) @extra
        if ($LASTEXITCODE) { $rc = 1 }
    }
} finally { Pop-Location }
exit $rc
