@echo off
setlocal enabledelayedexpansion
title C-BTree Exchange Setup and Runner

echo ====================================================
echo       C-BTree Exchange - Windows Setup
echo ====================================================
echo.

:: Change directory to current script folder
cd /d "%~dp0"
if exist "%~dp0C_BTree_Exchange\main.c" (
    cd /d "%~dp0C_BTree_Exchange"
)

:: Step 1: Check if gcc is in system PATH
where gcc >nul 2>nul
if %errorlevel% equ 0 (
    echo [INFO] GCC is already installed in system PATH.
    goto COMPILE
)

:: Step 2: Check common installed MinGW/MSYS2 directories on this PC
echo [INFO] Searching for existing MinGW / MSYS2 installations...

for %%P in (
    "D:\tools\msys64\ucrt64\bin"
    "C:\tools\msys64\ucrt64\bin"
    "C:\msys64\ucrt64\bin"
    "C:\msys64\mingw64\bin"
    "C:\MinGW\bin"
    "C:\Program Files\CodeBlocks\MinGW\bin"
    "C:\TDM-GCC-64\bin"
) do (
    if exist "%%~P\gcc.exe" (
        echo [INFO] Found GCC at: %%~P
        set "PATH=%%~P;!PATH!"
        goto COMPILE
    )
)

:: Step 3: If not found anywhere, offer automatic installation via winget
echo [WARN] GCC compiler not found on this PC.
echo.

where winget >nul 2>nul
if %errorlevel% equ 0 (
    echo [INFO] Windows Package Manager (winget) is available.
    echo [INFO] Installing MSYS2 GCC automatically...
    winget install -e --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
    if exist "C:\msys64\ucrt64\bin\gcc.exe" (
        set "PATH=C:\msys64\ucrt64\bin;!PATH!"
        goto COMPILE
    )
)

echo.
echo [ERROR] No C compiler found.
echo Please install MinGW or MSYS2 on this PC to compile:
echo   Option 1: Run 'winget install MSYS2.MSYS2'
echo   Option 2: Install Code::Blocks or MinGW
echo   Option 3: Or run under WSL by typing 'wsl'
pause
exit /b 1

:COMPILE
echo.
echo [INFO] Compiling C-BTree Exchange...
gcc -std=c11 -Wall -Wextra -pedantic -O2 main.c structures.c engine.c data_gen.c -o exchange.exe

if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Compilation failed.
    pause
    exit /b 1
)

echo [INFO] Compilation successful!
echo.
echo ====================================================
echo Launching C-BTree Exchange Engine...
echo ====================================================
echo.

exchange.exe

pause
