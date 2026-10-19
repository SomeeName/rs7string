<#
    Build rs7string: DLL (bin\Release\dinput8.dll) and unit tests (bin\Release\rs7string_tests.exe),
    then run the unit tests. Never copies anything into the game folder.
      .\build.ps1                build + run unit tests + static analysis
      .\build.ps1 -NoTests       build only (with -Asan: skips the normal test run, still runs the ASan tests)
      .\build.ps1 -Asan          also build the unit tests with AddressSanitizer (asan.props, bin\Asan, never the
                                 DLL) and run them; any ASan report or test failure fails the build
      .\build.ps1 -SkipAnalysis  no static analysis (quick local loop only, not for a pull request)
      .\build.ps1 -Toolset v143  another platform toolset than v145; -Toolset auto: v145 if installed, else the
                                 newest (CI runners without VS 2026)
    The build fails first when a source file is not formatted (scripts\format.ps1 -Check; fix with
    scripts\format.ps1) or breaks a layout rule (tools\check_layout.py, AGENTS.md "Code structure").
    -SkipFormatCheck skips both gates.
    Last, MSVC static analysis (/analyze, analyze.props) compiles both projects into obj\analyze and
    tools\check_analyze.py compares the findings with tools\analyze_baseline.json. About 2 minutes from clean,
    seconds when little changed.
      .\build.ps1 -Fuzz          afterwards: the parser fuzz targets, 30 s each (scripts\fuzz.ps1; libFuzzer + ASan,
                                 its own project and output folder bin\Fuzz, never part of the default build)
#>
param([switch]$NoTests, [switch]$SkipFormatCheck, [switch]$SkipAnalysis, [switch]$Asan, [switch]$Fuzz,
      [string]$Toolset = 'v145')
$ErrorActionPreference = 'Stop'
$MSBuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $MSBuild)) {
    # Not the VS 2026 Build Tools layout (a CI runner, another edition): the newest Visual Studio with MSBuild.
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $MSBuild = if (Test-Path -LiteralPath $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
    if (-not $MSBuild) { throw 'MSBuild not found: install the Visual Studio Build Tools (C++ workload)' }
}
if ($Toolset -eq 'auto') {
    # v145 when installed, else the newest Win32 platform toolset of that Visual Studio (CI runners).
    $vcTargets = [IO.Path]::GetFullPath((Join-Path (Split-Path $MSBuild -Parent) '..\..\Microsoft\VC'))
    $found = Get-ChildItem -Path "$vcTargets\*\Platforms\Win32\PlatformToolsets\*" -Directory -ErrorAction SilentlyContinue |
        ForEach-Object Name | Where-Object { $_ -match '^v\d+$' } | Sort-Object { [int]$_.Substring(1) } -Descending -Unique
    $Toolset = if ($found -contains 'v145') { 'v145' } else { $found | Select-Object -First 1 }
    if (-not $Toolset) { throw "no Win32 platform toolset found under $vcTargets" }
    Write-Host "platform toolset: $Toolset (installed: $($found -join ', '))"
}
$Common = @('/p:Configuration=Release', '/p:Platform=Win32', '/p:PostBuildEventUseInBuild=false', "/p:PlatformToolset=$Toolset", '/m', '/nologo')
Push-Location $PSScriptRoot
try {
    if (-not $SkipFormatCheck) {
        & "$PSScriptRoot\scripts\format.ps1" -Check
        if ($LASTEXITCODE) { throw 'format check failed: run scripts\format.ps1' }
        uv run python tools\check_layout.py
        if ($LASTEXITCODE) { throw 'layout check failed: see AGENTS.md "Code structure"' }
    }
    uv run python tools\gen_sites.py sites.json src\generated\sites.generated.h
    if ($LASTEXITCODE) { throw 'gen_sites failed' }
    foreach ($proj in 'rs7string.vcxproj', 'tests\rs7string_tests.vcxproj') {
        & $MSBuild $proj @Common /v:minimal
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
    if (-not $SkipAnalysis) {
        foreach ($proj in 'rs7string.vcxproj', 'tests\rs7string_tests.vcxproj') {
            & $MSBuild $proj /t:ClCompile /p:Rs7Analyze=true @Common /v:quiet
            if ($LASTEXITCODE) { throw "analysis build failed: $proj" }
        }
        uv run python tools\check_analyze.py
        if ($LASTEXITCODE) { throw 'static analysis: new findings, see AGENTS.md "Build and gates"' }
    }
    if ($Asan) {
        & $MSBuild 'tests\rs7string_tests.vcxproj' /p:Rs7Asan=true @Common /v:minimal
        if ($LASTEXITCODE) { throw 'ASan build failed: tests\rs7string_tests.vcxproj' }
        # The x86 ASan runtime of the newest installed MSVC toolset (VC\Tools\MSVC\<version>), next to the exe.
        $vc = [IO.Path]::GetFullPath((Join-Path (Split-Path $MSBuild -Parent) '..\..\..\VC\Tools\MSVC'))
        if (-not (Test-Path -LiteralPath $vc)) { throw "MSVC tools folder not found next to MSBuild ($vc): install the C++ build tools" }
        $rt = Get-ChildItem -LiteralPath $vc -Directory | Where-Object { $_.Name -as [version] } |
            Sort-Object { [version]$_.Name } -Descending |
            ForEach-Object { Join-Path $_.FullName 'bin\Hostx64\x86\clang_rt.asan_dynamic-i386.dll' } |
            Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        if (-not $rt) { throw "ASan runtime (clang_rt.asan_dynamic-i386.dll) not found under ${vc}: install the MSVC AddressSanitizer component" }
        Copy-Item -LiteralPath $rt -Destination "$PSScriptRoot\bin\Asan" -Force
        & "$PSScriptRoot\bin\Asan\rs7string_tests.exe"
        if ($LASTEXITCODE) { throw 'unit tests under ASan failed' }
    }
    if ($Fuzz) {
        & "$PSScriptRoot\scripts\fuzz.ps1" -Seconds 30 -Toolset $Toolset -MSBuild $MSBuild
        if ($LASTEXITCODE) { throw 'fuzzing found a crash, leak or timeout: see obj\fuzz\artifacts' }
    }
} finally { Pop-Location }
