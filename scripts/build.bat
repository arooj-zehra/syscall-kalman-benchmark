@echo off
rem Configure, build, test and run the smoke test on Windows (MSYS2 MinGW-w64 in PATH).
rem Usage: scripts\build.bat [Release|Debug]
setlocal
cd /d "%~dp0\.."

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release

where gcc >nul 2>nul
if errorlevel 1 (
    echo ERROR: gcc not found in PATH. Add your MSYS2 ucrt64\bin or mingw64\bin folder to PATH.
    exit /b 1
)

where ninja >nul 2>nul
if not errorlevel 1 (
    set GENERATOR=Ninja
    goto configure
)
where mingw32-make >nul 2>nul
if not errorlevel 1 (
    set GENERATOR=MinGW Makefiles
    goto configure
)
echo ERROR: neither ninja nor mingw32-make found in PATH.
echo        Install one in the MSYS2 shell that matches your gcc, for example:
echo          pacman -S mingw-w64-ucrt-x86_64-ninja     (UCRT64 shell)
echo          pacman -S mingw-w64-x86_64-ninja          (MINGW64 shell)
exit /b 1

:configure
echo == Configure (%GENERATOR%, %BUILD_TYPE%)
cmake -S . -B build -G "%GENERATOR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_C_COMPILER=gcc
if errorlevel 1 exit /b 1

echo == Build
cmake --build build
if errorlevel 1 exit /b 1

echo == Test
ctest --test-dir build --output-on-failure
if errorlevel 1 exit /b 1

echo == Smoke test output
build\bin\hello.exe build\hello.log
exit /b %errorlevel%
