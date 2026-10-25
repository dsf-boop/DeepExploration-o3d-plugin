@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo  Building OMSI 2 O3D Plugin for Deep Exploration
echo ===================================================

:: 1. Check if CXX environment variable is already set
if not "%CXX%"=="" (
    if exist "%CXX%" goto :compiler_found
    where "%CXX%" >nul 2>nul && goto :compiler_found
)

:: 2. Check 32-bit MinGW / LLVM-MinGW GCC in PATH
where i686-w64-mingw32-g++.exe >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "CXX=i686-w64-mingw32-g++"
    goto :compiler_found
)

:: 3. Check generic g++ in PATH
where g++.exe >nul 2>nul
if %ERRORLEVEL% equ 0 (
    set "CXX=g++"
    goto :compiler_found
)

:: 4. Check common MSYS2 / MinGW / LLVM-MinGW installation paths
if exist "C:\msys64\mingw32\bin\g++.exe" (
    set "CXX=C:\msys64\mingw32\bin\g++.exe"
    goto :compiler_found
)
if exist "C:\MinGW\bin\g++.exe" (
    set "CXX=C:\MinGW\bin\g++.exe"
    goto :compiler_found
)
if exist "C:\llvm-mingw\bin\i686-w64-mingw32-g++.exe" (
    set "CXX=C:\llvm-mingw\bin\i686-w64-mingw32-g++.exe"
    goto :compiler_found
)

:: 5. Check WinGet LLVM-MinGW package in LOCALAPPDATA
if defined LOCALAPPDATA (
    for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\*llvm-mingw*") do (
        for /d %%S in ("%%D\llvm-mingw*") do (
            if exist "%%S\bin\i686-w64-mingw32-g++.exe" (
                set "CXX=%%S\bin\i686-w64-mingw32-g++.exe"
                goto :compiler_found
            )
        )
    )
)

echo ERROR: No 32-bit C++ compiler (i686-w64-mingw32-g++ or g++) found!
echo Please install MinGW-w64 (32-bit) or set the CXX environment variable, e.g.:
echo   set CXX=C:\path\to\i686-w64-mingw32-g++.exe
exit /b 1

:compiler_found
echo Using compiler: %CXX%

if not exist dist mkdir dist

echo Compiling o3d_plugin.cpp...
"%CXX%" -fms-extensions -shared -O2 -m32 -static -Wl,--kill-at plugin.def o3d_plugin.cpp -o dist\o3d.x3d
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo [SUCCESS] Plugin successfully built: dist\o3d.x3d
exit /b 0
