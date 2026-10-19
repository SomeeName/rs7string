<#
    Format (default) or check (-Check) the C++ and Python sources.
      .\scripts\format.ps1           rewrite files in place (clang-format, tools\blank_lines.py, ruff format, ruff check --fix)
      .\scripts\format.ps1 -Check    change nothing, list unformatted files, exit 1 if there are any
    Config: .clang-format (braces, layout), tools\blank_lines.py (blank lines between statements, AGENTS.md
    "Code structure"), [tool.ruff] in pyproject.toml. Generated files are never formatted: fix the
    generator (tools\gen_*.py) instead.
#>
param([switch]$Check)
$ErrorActionPreference = 'Stop'

# Permanently excluded: vendored code, and generated files (their generators own the layout).
$PermanentExclude = @(
    'third_party/'
    '.generated.h'
)

function Test-Excluded([string]$Path, [string[]]$List) {
    foreach ($e in $List) {
        if ($e.EndsWith('/')) { if ($Path.StartsWith($e)) { return $true } }
        elseif ($Path.EndsWith($e)) { return $true }
    }
    return $false
}

function Find-ClangFormat {
    $cmd = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($root in "${env:ProgramFiles}\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio") {
        if (Test-Path $root) {
            $hit = Get-ChildItem -Path $root -Recurse -Filter clang-format.exe -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
    }
    throw 'clang-format not found: put it on PATH (e.g. scoop install llvm) or install the VS "C++ Clang tools" component'
}

Push-Location (Join-Path $PSScriptRoot '..')
try {
    $tracked = @(git ls-files) | ForEach-Object { $_.Replace('\', '/') }
    $keep = { param($p) -not (Test-Excluded $p $PermanentExclude) }
    $cpp = @($tracked | Where-Object { $_ -match '^(src|tests|fuzz)/.*\.(cpp|h)$' -and (& $keep $_) })
    $py = @($tracked | Where-Object { $_ -match '^(tools|scripts)/.*\.py$' -and (& $keep $_) })
    $bad = 0

    $cf = Find-ClangFormat
    if ($Check) {
        & $cf --dry-run -Werror --style=file @cpp
        if ($LASTEXITCODE) { $bad++ }
        uv run python tools/blank_lines.py --check @cpp
        if ($LASTEXITCODE) { $bad++ }
        uv run ruff format --check @py
        if ($LASTEXITCODE) { $bad++ }
        uv run ruff check @py
        if ($LASTEXITCODE) { $bad++ }
        if ($bad) { Write-Error 'format check failed: run .\scripts\format.ps1' -ErrorAction Continue; exit 1 }
        Write-Host "format ok ($($cpp.Count) C++ files, $($py.Count) Python files)"
    } else {
        & $cf -i --style=file @cpp
        if ($LASTEXITCODE) { throw 'clang-format failed' }
        # Blank lines between statements, then clang-format again (it re-aligns trailing comments the new blank lines split).
        uv run python tools/blank_lines.py @cpp
        if ($LASTEXITCODE) { throw 'blank_lines failed' }
        & $cf -i --style=file @cpp
        if ($LASTEXITCODE) { throw 'clang-format failed' }
        uv run ruff format @py
        if ($LASTEXITCODE) { throw 'ruff format failed' }
        uv run ruff check --fix @py
        if ($LASTEXITCODE) { throw 'ruff check found problems it cannot fix' }
    }
} finally { Pop-Location }
