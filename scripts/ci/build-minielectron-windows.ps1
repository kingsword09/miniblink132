param(
    [string]$Configuration = "Release",
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$solution = Join-Path $repoRoot "build\miniblink132.sln"
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

function Get-VisualStudioPath {
    $path = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1
    if (-not $path) {
        throw "Visual Studio with MSBuild was not found."
    }

    return $path
}

function Get-MSBuildPath {
    $path = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\amd64\MSBuild.exe" | Select-Object -First 1
    if ($path) {
        return $path
    }

    $path = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\MSBuild.exe" | Select-Object -First 1
    if (-not $path) {
        throw "MSBuild.exe not found. Install Visual Studio Build Tools with MSBuild."
    }

    return $path
}

function Get-LLVMInstall {
    param([string]$VisualStudioPath)

    $candidates = @()
    if ($env:MINIBLINK_LLVM_INSTALL_DIR) {
        $candidates += $env:MINIBLINK_LLVM_INSTALL_DIR
    }

    $candidates += @(
        "C:\Program Files\LLVM",
        (Join-Path $VisualStudioPath "VC\Tools\Llvm\x64"),
        (Join-Path $VisualStudioPath "VC\Tools\Llvm")
    )

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not $candidate) {
            continue
        }

        $clangCl = Join-Path $candidate "bin\clang-cl.exe"
        $clangLib = Join-Path $candidate "lib\clang"
        if ((Test-Path $clangCl) -and (Test-Path $clangLib)) {
            return (Resolve-Path $candidate).Path
        }
    }

    throw "LLVM was not found. Set MINIBLINK_LLVM_INSTALL_DIR or install LLVM/ClangCL on the runner."
}

function Get-LLVMToolsVersion {
    param([string]$LLVMInstallDir)

    $clangLib = Join-Path $LLVMInstallDir "lib\clang"
    $versions = Get-ChildItem -Path $clangLib -Directory |
        Sort-Object -Property @{ Expression = {
            try {
                [version]$_.Name
            } catch {
                [version]"0.0"
            }
        }; Descending = $true }

    if (-not $versions) {
        throw "No clang resource directory was found under $clangLib."
    }

    return $versions[0].Name
}

function Ensure-NASM {
    $nasm = Get-Command nasm -ErrorAction SilentlyContinue
    if (-not $nasm) {
        foreach ($candidate in @("C:\Program Files\NASM\nasm.exe", "C:\Program Files (x86)\NASM\nasm.exe")) {
            if (Test-Path $candidate) {
                $env:PATH = "$(Split-Path $candidate -Parent);$env:PATH"
                $nasm = Get-Command nasm -ErrorAction SilentlyContinue
                break
            }
        }
    }

    if ((-not $nasm) -and (Get-Command choco -ErrorAction SilentlyContinue)) {
        choco install nasm -y --no-progress
        $env:PATH = "C:\Program Files\NASM;$env:PATH"
        $nasm = Get-Command nasm -ErrorAction SilentlyContinue
    }

    if (-not $nasm) {
        throw "nasm.exe was not found. Install NASM or make it available on PATH."
    }

    $env:NASMPATH = Split-Path $nasm.Source -Parent
    Write-Host "Using NASM: $($nasm.Source)"
}

function Ensure-VSNASMBuildCustomization {
    $customizationDir = Join-Path ${env:ProgramFiles(x86)} "MSBuild\Microsoft.Cpp\v4.0\V140\BuildCustomizations"
    $requiredFiles = @("nasm.props", "nasm.targets", "nasm.xml")
    $missingFiles = $requiredFiles | Where-Object { -not (Test-Path (Join-Path $customizationDir $_)) }

    if (-not $missingFiles) {
        return
    }

    $tempRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() }
    $workDir = Join-Path $tempRoot "VSNASM"
    if (Test-Path $workDir) {
        Remove-Item -Path $workDir -Recurse -Force
    }

    git clone --depth 1 --branch 2.0 https://github.com/ShiftMediaProject/VSNASM.git $workDir
    New-Item -ItemType Directory -Path $customizationDir -Force | Out-Null

    foreach ($file in $requiredFiles) {
        Copy-Item -Path (Join-Path $workDir $file) -Destination $customizationDir -Force
    }
}

function Ensure-LibwebpConfig {
    $configDir = Join-Path $repoRoot "third_party\libwebp\src\src\webp"
    $configTemplate = Join-Path $repoRoot "scripts\ci\libwebp-config.h"
    $configFile = Join-Path $configDir "config.h"

    New-Item -ItemType Directory -Path $configDir -Force | Out-Null
    Copy-Item -Path $configTemplate -Destination $configFile -Force
}

if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. This workflow requires a Visual Studio hosted runner."
}

$visualStudioPath = Get-VisualStudioPath
$msbuild = Get-MSBuildPath
$llvmInstallDir = Get-LLVMInstall -VisualStudioPath $visualStudioPath
$llvmToolsVersion = Get-LLVMToolsVersion -LLVMInstallDir $llvmInstallDir

Ensure-NASM
Ensure-VSNASMBuildCustomization
Ensure-LibwebpConfig

Write-Host "Using MSBuild: $msbuild"
Write-Host "Using LLVM: $llvmInstallDir"
Write-Host "Using LLVMToolsVersion: $llvmToolsVersion"

& $msbuild $solution `
    /m `
    /t:minielectron `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /p:PreferredToolArchitecture=x64 `
    /p:WindowsTargetPlatformVersion=10.0 `
    /p:LLVMInstallDir="$llvmInstallDir" `
    /p:LLVMToolsVersion="$llvmToolsVersion" `
    /v:minimal

if ($LASTEXITCODE -ne 0) {
    throw "MSBuild failed with exit code $LASTEXITCODE"
}

$outDir = Join-Path $repoRoot "out\win_${Configuration}_${Platform}"
$exe = Join-Path $outDir "minielectron_*.exe"
if (-not (Get-ChildItem -Path $exe -ErrorAction SilentlyContinue)) {
    throw "MiniElectron executable was not produced under $outDir"
}
