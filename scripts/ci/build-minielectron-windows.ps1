param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$solution = Join-Path $repoRoot "build\miniblink132.sln"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. This workflow requires a Visual Studio hosted runner."
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\MSBuild.exe" | Select-Object -First 1
if (-not $msbuild) {
    throw "MSBuild.exe not found. Install Visual Studio Build Tools with MSBuild."
}

& $msbuild $solution `
    /m `
    /t:minielectron `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /p:PreferredToolArchitecture=x64 `
    /p:WindowsTargetPlatformVersion=10.0 `
    /v:minimal

$outDir = Join-Path $repoRoot "out\win_${Configuration}_${Platform}"
$exe = Join-Path $outDir "minielectron_*.exe"
if (-not (Get-ChildItem -Path $exe -ErrorAction SilentlyContinue)) {
    throw "MiniElectron executable was not produced under $outDir"
}
