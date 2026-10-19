<#
    Run pytest as CI does: with RS7_GAME_FILES pointing at an empty folder, so every test that needs the user's
    game files (tools/game_files.py) must skip cleanly. The variable is set for this call only.
      .\scripts\pytest_nogame.ps1                 pytest tools\tests -q -rs
      .\scripts\pytest_nogame.ps1 -k colournative  extra arguments go to pytest
#>
$ErrorActionPreference = 'Stop'
$empty = Join-Path ([IO.Path]::GetTempPath()) 'rs7-no-game-files'
New-Item -ItemType Directory -Force $empty | Out-Null
if (Get-ChildItem -LiteralPath $empty -Force) { throw "$empty is not empty" }
$previous = $env:RS7_GAME_FILES
$env:RS7_GAME_FILES = $empty
Push-Location (Join-Path $PSScriptRoot '..')
try {
    uv run pytest tools\tests -q -rs @args
    exit $LASTEXITCODE
} finally {
    Pop-Location
    $env:RS7_GAME_FILES = $previous
}
