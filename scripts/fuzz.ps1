<#
    Fuzz the parsers of untrusted input (fuzz\, docs/DEVELOPMENT.md "Fuzzing"): libFuzzer + AddressSanitizer from the
    MSVC Build Tools (x86), one exe, one target per run. Never part of the default build.
      .\scripts\fuzz.ps1                         every target 30 s (what build.ps1 -Fuzz runs)
      .\scripts\fuzz.ps1 -Seconds 3600 -Target rs7x,recipe
      .\scripts\fuzz.ps1 -NoBuild -Target ini -Seconds 600
    Corpus (grows across runs): obj\fuzz\corpus\<target>; seeds: obj\fuzz\seeds\<target> (built-in, from the
    exe, plus repository files); crashes, leaks and timeouts: obj\fuzz\artifacts\<target>\. Replay one input:
      $env:RS7_FUZZ_TARGET = 'rs7x'; bin\Fuzz\rs7string_fuzz.exe -artifact_prefix=obj\fuzz\artifacts\rs7x\ obj\fuzz\artifacts\rs7x\crash-<sha1>
    Fixed seed (-seed=1) for a reproducible short run; exit code 1 when a target found something.
#>
param([int]$Seconds = 30, [string[]]$Target = @(), [switch]$NoBuild, [string]$Toolset = 'v145',
      [string]$MSBuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$all = 'rs7x', 'dxt', 'settings', 'ini', 'text', 'package', 'nif', 'dds', 'recipe'
$targets = if ($Target.Count) { $Target | ForEach-Object { $_ -split ',' } } else { $all }
$exe = Join-Path $repo 'bin\Fuzz\rs7string_fuzz.exe'
$work = Join-Path $repo 'obj\fuzz'

if (-not $NoBuild) {
    & $MSBuild (Join-Path $repo 'fuzz\rs7string_fuzz.vcxproj') /p:Configuration=Release /p:Platform=Win32 `
        /p:PostBuildEventUseInBuild=false "/p:PlatformToolset=$Toolset" /m /v:minimal /nologo
    if ($LASTEXITCODE) { throw 'fuzz build failed' }
}

# The ASan runtime DLL of the same toolset (clang_rt.asan_dynamic-i386.dll).
$vc = Split-Path (Split-Path (Split-Path (Split-Path $MSBuild)))
$tools = Get-ChildItem (Join-Path $vc 'VC\Tools\MSVC') | Sort-Object Name | Select-Object -Last 1
$env:PATH = (Join-Path $tools.FullName 'bin\Hostx64\x86') + ';' + $env:PATH

function Add-Seed([string]$dir, [string]$name, [byte[]]$prefix, [string]$file, [int]$max) {
    $bytes = [IO.File]::ReadAllBytes($file)
    if ($max -and $bytes.Length -gt $max) { $bytes = $bytes[0..($max - 1)] }
    [IO.File]::WriteAllBytes((Join-Path $dir $name), [byte[]]($prefix + $bytes))
}

$failed = @()
foreach ($t in $targets) {
    if ($all -notcontains $t) { throw "unknown target $t (one of: $($all -join ', '))" }
    $corpus = Join-Path $work "corpus\$t"
    $seeds = Join-Path $work "seeds\$t"
    $artifacts = Join-Path $work "artifacts\$t"
    foreach ($d in $corpus, $seeds, $artifacts) { New-Item -ItemType Directory -Force $d | Out-Null }
    $env:RS7_FUZZ_TARGET = $t
    $env:RS7_FUZZ_SEEDS = $seeds
    & $exe | Out-Host
    $seeded = $LASTEXITCODE
    Remove-Item Env:\RS7_FUZZ_SEEDS
    if ($seeded) { throw "writing the built-in seeds of $t failed (exit $seeded)" }
    switch ($t) {
        'package' {  # the start of both headstock packages (the manifest is at the front)
            Add-Seed $seeds 'headstock.bin' @() (Join-Path $repo 'data\headstock\rs7string_headstock.bin') 65536
            Add-Seed $seeds 'headstock_lh.bin' @() (Join-Path $repo 'data\headstock\rs7string_headstock_lh.bin') 65536
        }
        'recipe' {  # the shipped recipes without their game movie: the header checks
            foreach ($r in Get-ChildItem (Join-Path $repo 'data\chord-diagrams') -Filter *.rs7da) {
                Add-Seed $seeds $r.Name ([byte[]](0, 0, 0, 0, 0, 0)) $r.FullName 0
            }
        }
    }
    Write-Host "== $t ($Seconds s)"
    # libFuzzer writes to stderr: under Windows PowerShell 5.1, 2>&1 turns each line into an error record, which
    # 'Stop' would throw on.
    $ErrorActionPreference = 'Continue'
    & $exe "-max_total_time=$Seconds" '-seed=1' '-timeout=10' '-rss_limit_mb=2048' '-max_len=70000' `
        "-artifact_prefix=$artifacts\" '-print_final_stats=1' $corpus $seeds 2>&1 |
        Select-String -Pattern 'ERROR|SUMMARY|Done \d+ runs|stat::number_of_executed_units|crash-|leak-|timeout-|oom-' |
        ForEach-Object { Write-Host "  $($_.Line)" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code) { $failed += $t }
}
Remove-Item Env:\RS7_FUZZ_TARGET -ErrorAction SilentlyContinue
if ($failed.Count) { Write-Host "fuzz: findings in $($failed -join ', ') (see $work\artifacts)"; exit 1 }
Write-Host "fuzz: $($targets.Count) target(s), no findings"
