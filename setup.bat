@echo off
setlocal enabledelayedexpansion
title C-BTree Exchange Setup and Runner

echo ====================================================
echo       C-BTree Exchange - Windows Setup
echo ====================================================
echo.

:: Check if C_BTree_Exchange subdirectory exists, otherwise use current folder
if exist "%~dp0C_BTree_Exchange\main.c" (
    cd /d "%~dp0C_BTree_Exchange"
) else (
    cd /d "%~dp0"
)

:: Step 1: Check if gcc is already available
where gcc >nul 2>nul
if %errorlevel% equ 0 (
    echo [INFO] GCC is already installed on this machine.
    goto COMPILE
)

echo [WARN] GCC compiler not detected in system PATH.
echo [INFO] Setting up lightweight portable C compiler...
echo.

set "COMPILER_DIR=%~dp0compiler"
set "COMPILER_ZIP=%TEMP%\w64devkit.zip"

if exist "%COMPILER_DIR%\bin\gcc.exe" (
    echo [INFO] Existing portable compiler found in %COMPILER_DIR%.
    goto ADD_PATH
)

echo [INFO] Downloading portable MinGW compiler (w64devkit)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; (New-Object System.Net.WebClient).DownloadFile('https://github.com/skeeto/w64devkit/releases/download/v1.20.0/w64devkit-1.20.0.zip', '%COMPILER_ZIP%')"

if not exist "%COMPILER_ZIP%" (
    echo [ERROR] Failed to download portable compiler.
    echo Please make sure this PC is connected to the internet.
    pause
    exit /b 1
)

echo [INFO] Extracting compiler...
tar -xf "%COMPILER_ZIP%" -C "%TEMP%"
if exist "%TEMP%\w64devkit" (
    if not exist "%COMPILER_DIR%" mkdir "%COMPILER_DIR%"
    xcopy /E /I /Q /Y "%TEMP%\w64devkit\*" "%COMPILER_DIR%\" >nul
    rmdir /S /Q "%TEMP%\w64devkit" >nul 2>nul
)
del "%COMPILER_ZIP%" >nul 2>nul

:ADD_PATH
set "PATH=%COMPILER_DIR%\bin;%PATH%"

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
