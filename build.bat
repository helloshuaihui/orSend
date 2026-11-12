@echo off
rem ============================================================
rem  orSend 一键构建脚本（Windows / MSVC）
rem  用法: build.bat [Debug^|Release]
rem ============================================================
setlocal enabledelayedexpansion

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] 未找到 vswhere.exe，请确认已安装 Visual Studio 2022。
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"

if "%VSPATH%"=="" (
    echo [ERROR] 未找到带 C++ 工具集的 Visual Studio 安装。
    exit /b 1
)

echo [1/3] 初始化 MSVC 环境: %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat 执行失败。
    exit /b 1
)

echo [2/3] 生成 CMake 工程
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake 配置失败。
    exit /b 1
)

echo [3/3] 编译 (%CONFIG%)
cmake --build build --config %CONFIG% --parallel
if errorlevel 1 (
    echo [ERROR] 编译失败。
    exit /b 1
)

echo.
echo 构建完成: build\bin\orsend.exe
endlocal
