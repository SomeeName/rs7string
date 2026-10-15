<#
    Build rs7string: DLL (bin\Release\dinput8.dll) and unit tests (bin\Release\rs7string_tests.exe),
    then run the unit tests. Never copies anything into the game folder.
      .\build.ps1            build + run unit tests
      .\build.ps1 -NoTests   build only
    The build fails first when a source file is not formatted (scripts\format.ps1 -Check; fix with
    scripts\format.ps1). -SkipFormatCheck skips that gate.
#>
param([switch]$NoTests, [switch]$SkipFormatCheck)
$ErrorActionPreference = 'Stop'
$MSBuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
Push-Location $PSScriptRoot
try {
    if (-not $SkipFormatCheck) {
        & "$PSScriptRoot\scripts\format.ps1" -Check
        if ($LASTEXITCODE) { throw 'format check failed: run scripts\format.ps1' }
    }
    uv run python tools\gen_sites.py sites.json src\generated\sites.generated.h
    if ($LASTEXITCODE) { throw 'gen_sites failed' }
    foreach ($proj in 'rs7string.vcxproj', 'tests\rs7string_tests.vcxproj') {
        & $MSBuild $proj /p:Configuration=Release /p:Platform=Win32 /p:PostBuildEventUseInBuild=false /p:PlatformToolset=v145 /m /v:minimal /nologo
        if ($LASTEXITCODE) { throw "build failed: $proj" }
    }
    Copy-Item -LiteralPath "$PSScriptRoot\data\headstock\rs7string_headstock.bin" -Destination "$PSScriptRoot\bin\Release\rs7string_headstock.bin"
    Copy-Item -LiteralPath "$PSScriptRoot\data\headstock\rs7string_headstock_lh.bin" -Destination "$PSScriptRoot\bin\Release\rs7string_headstock_lh.bin"
    Copy-Item -LiteralPath "$PSScriptRoot\data\headstock\LICENSE.md" -Destination "$PSScriptRoot\bin\Release\rs7string_headstock.LICENSE.md"
    if (-not $NoTests) {
        & "$PSScriptRoot\bin\Release\rs7string_tests.exe"
        if ($LASTEXITCODE) { throw 'unit tests failed' }
    }
    Copy-Item -LiteralPath "$PSScriptRoot\data\chord-diagrams" -Destination "$PSScriptRoot\bin\Release" -Recurse -Force
} finally { Pop-Location }
