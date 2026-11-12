# ============================================================
#  orSend 一键构建脚本（Windows / MSVC）
#
#  用法:
#    powershell -ExecutionPolicy Bypass -File build.ps1
#    powershell -ExecutionPolicy Bypass -File build.ps1 -Config Debug
#
#  说明:
#    部分机器上 Visual Studio 的 C++ 工具集没有注册到 vswhere，
#    或 Windows SDK 未被 vcvars 识别，本脚本会自动补齐 INCLUDE/LIB/PATH。
# ============================================================
param(
    [string]$Config = "Release",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $root

# vcvars 会写入大写形式的环境变量，与已存在的小写代理变量冲突，先清理
foreach ($name in @('https_proxy', 'http_proxy', 'no_proxy', 'all_proxy')) {
    if (Test-Path "env:$name") { Remove-Item "env:$name" -ErrorAction SilentlyContinue }
}

# ---- 1. 定位 Visual Studio ----
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
if (-not $vsPath) {
    foreach ($p in @("${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
                     "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
                     "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise")) {
        if (Test-Path "$p\VC\Auxiliary\Build\vcvars64.bat") { $vsPath = $p; break }
    }
}
if (-not $vsPath) {
    Write-Host "[ERROR] 未找到 Visual Studio 2022 C++ 工具集，请先安装「使用 C++ 的桌面开发」工作负载。" -ForegroundColor Red
    exit 1
}

Write-Host "[1/3] 初始化 MSVC 环境: $vsPath" -ForegroundColor Cyan
$devShell = "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
if (Test-Path $devShell) {
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
}

# ---- 2. 补齐 Windows SDK（vcvars 有时无法识别未注册的 SDK）----
$sdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
if (Test-Path "$sdkRoot\Include") {
    $sdkVer = (Get-ChildItem "$sdkRoot\Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
    if ($sdkVer) {
        $env:INCLUDE = "$sdkRoot\Include\$sdkVer\ucrt;$sdkRoot\Include\$sdkVer\um;$sdkRoot\Include\$sdkVer\shared;$env:INCLUDE"
        $env:LIB = "$sdkRoot\Lib\$sdkVer\ucrt\x64;$sdkRoot\Lib\$sdkVer\um\x64;$env:LIB"
        $env:PATH = "$sdkRoot\bin\$sdkVer\x64;$env:PATH"
    }
}

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $cl) {
    Write-Host "[ERROR] 未找到 cl.exe，MSVC 环境初始化失败。" -ForegroundColor Red
    exit 1
}

# ---- 3. 配置并编译 ----
$ninja = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (Test-Path $ninja) {
    Write-Host "[2/3] 生成 CMake 工程 (Ninja)" -ForegroundColor Cyan
    & cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_MAKE_PROGRAM=$ninja"
} else {
    Write-Host "[2/3] 生成 CMake 工程 (Visual Studio 17 2022)" -ForegroundColor Cyan
    & cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64
}
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] CMake 配置失败。" -ForegroundColor Red
    exit 1
}

Write-Host "[3/3] 编译 ($Config)" -ForegroundColor Cyan
& cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] 编译失败。" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "构建完成: $BuildDir\bin\orsend.exe" -ForegroundColor Green
