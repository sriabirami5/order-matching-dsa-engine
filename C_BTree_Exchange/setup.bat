@echo off
setlocal
title C-BTree Exchange Setup and Runner

echo ====================================================
echo       C-BTree Exchange - Windows Setup
echo ====================================================
echo.

cd /d "%~dp0"
if exist "%~dp0C_BTree_Exchange\main.c" cd /d "%~dp0C_BTree_Exchange"

REM Check 1: Check if gcc is already in PATH
where gcc >nul 2>nul
if %errorlevel% equ 0 goto FOUND_GCC_PATH

REM Check 2: Check common MinGW or MSYS2 installation folders
if exist "D:\tools\msys64\ucrt64\bin\gcc.exe" (
    set "PATH=D:\tools\msys64\ucrt64\bin;%PATH%"
    goto FOUND_GCC_DIR
)
if exist "C:\tools\msys64\ucrt64\bin\gcc.exe" (
    set "PATH=C:\tools\msys64\ucrt64\bin;%PATH%"
    goto FOUND_GCC_DIR
)
if exist "C:\msys64\ucrt64\bin\gcc.exe" (
    set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    goto FOUND_GCC_DIR
)
if exist "C:\msys64\mingw64\bin\gcc.exe" (
    set "PATH=C:\msys64\mingw64\bin;%PATH%"
    goto FOUND_GCC_DIR
)
if exist "C:\MinGW\bin\gcc.exe" (
    set "PATH=C:\MinGW\bin;%PATH%"
    goto FOUND_GCC_DIR
)
if exist "C:\Program Files\CodeBlocks\MinGW\bin\gcc.exe" (
    set "PATH=C:\Program Files\CodeBlocks\MinGW\bin;%PATH%"
    goto FOUND_GCC_DIR
)
if exist "C:\TDM-GCC-64\bin\gcc.exe" (
    set "PATH=C:\TDM-GCC-64\bin;%PATH%"
    goto FOUND_GCC_DIR
)

REM Check 3: If no GCC, check if exchange.exe already exists
if exist "exchange.exe" (
    echo [WARN] GCC compiler was not found on this PC.
    echo [INFO] Pre-compiled exchange.exe detected. Launching directly...
    echo.
    goto RUN_EXCHANGE
)

REM Check 4: If no compiler and no exchange.exe, report clearly
echo [ERROR] No C compiler was found on this PC.
echo.
echo To run this project on a PC without GCC:
echo   1. Copy the compiled exchange.exe into this folder, OR
echo   2. Install MinGW, MSYS2, or CodeBlocks, OR
echo   3. Run under WSL by typing: wsl
echo.
pause
exit /b 1

:FOUND_GCC_PATH
echo [INFO] Found GCC compiler in system PATH.
goto COMPILE

:FOUND_GCC_DIR
echo [INFO] Found installed GCC compiler on this machine.
goto COMPILE

:COMPILE
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

:RUN_EXCHANGE
echo ====================================================
echo Starting C-BTree Exchange Engine...
echo ====================================================
echo.

exchange.exe

pause
