@echo off
rem Run the whole benchmark suite on Windows and write results\windows.csv.
rem
rem Usage: scripts\run_bench.bat [RUNS] [--quick]
rem   RUNS     how many times to repeat the whole suite (default 5)
rem   --quick  tiny smoke-test sizes; the numbers are NOT valid measurements
rem
rem Before a real run: build in Release (scripts\build.bat), plug in the power,
rem close browsers and other heavy programs, and run from a normal (non-elevated)
rem command prompt, as for the demos.
setlocal
cd /d "%~dp0\.."

set "RUNS=5"
set "QUICK="
set "OUT=results\windows.csv"
if /i "%~1"=="--quick" (
    set "QUICK=--quick"
) else if not "%~1"=="" (
    set "RUNS=%~1"
)
if /i "%~2"=="--quick" set "QUICK=--quick"

if not exist build\bin\bench.exe (
    echo ERROR: build\bin\bench.exe not found. Build first:  scripts\build.bat
    exit /b 1
)
findstr /c:"CMAKE_BUILD_TYPE:STRING=Release" build\CMakeCache.txt >nul 2>nul
if errorlevel 1 echo WARNING: build is not Release; timings will be pessimistic.

if exist %OUT% (
    echo ERROR: %OUT% already exists. Delete or rename it first, then run again.
    exit /b 1
)
if not exist results mkdir results
if not exist results\tmp mkdir results\tmp

for /l %%R in (1,1,%RUNS%) do (
    echo =============== run %%R of %RUNS% ===============
    build\bin\bench.exe --out %OUT% --run %%R --tmpdir results\tmp %QUICK%
    if errorlevel 1 echo WARNING: run %%R reported failed cases
)

rmdir results\tmp 2>nul
echo.
echo Done. Results: %OUT%
echo Next: python bench\analyze.py %OUT% results\linux.csv --out results\plots
exit /b 0
