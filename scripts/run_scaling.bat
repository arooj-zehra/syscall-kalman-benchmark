@echo off
rem Scaling experiment on Windows: serial vs. 1..N workers, written to results\scaling_windows.csv.
rem
rem Usage: scripts\run_scaling.bat [preset] [runs] [rounds] [overwrite]
rem   preset     small | medium | large (default large: 256 tracks x 100,000 steps)
rem   runs       how many times to repeat the whole sweep (default 5)
rem   rounds     filter passes inside one process; the median is reported (default 3)
rem   overwrite  type "overwrite" to replace an existing results\scaling_windows.csv
rem Edit the WORKERS line below to change the worker counts.
rem
rem Every row is one process run. Parallel rows are checked against the serial result
rem (--expect-file) and are only written if the hash matches. No result file is written,
rem so disk speed is not part of the measurement. Plug in the power and close other
rem programs first.
setlocal enabledelayedexpansion
cd /d "%~dp0\.."

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=large"
set "RUNS=%~2"
if "%RUNS%"=="" set "RUNS=5"
set "ROUNDS=%~3"
if "%ROUNDS%"=="" set "ROUNDS=3"
set "WORKERS=1 2 3 4 5 6 7 8 12 16"
set "OUT=results\scaling_windows.csv"
set "BIN=build\bin"

for %%E in (kfgen kf_serial kf_master kf_worker) do (
    if not exist %BIN%\%%E.exe (
        echo ERROR: %BIN%\%%E.exe missing. Build first:  scripts\build.bat
        exit /b 1
    )
)
if exist %OUT% (
    if /i not "%~4"=="overwrite" (
        echo ERROR: %OUT% exists. Add "overwrite" as the 4th argument to replace it.
        exit /b 1
    )
    del %OUT%
)
if not exist data mkdir data
if not exist results mkdir results

set "IN=data\meas_%PRESET%.bin"
set "REF=data\est_serial_%PRESET%.bin"
if not exist %IN% (
    echo == generating %IN%
    %BIN%\kfgen.exe --preset %PRESET% --out %IN%
    if errorlevel 1 exit /b 1
)
if not exist %REF% (
    echo == computing the serial reference %REF%
    %BIN%\kf_serial.exe --in %IN% --out %REF% >nul
    if errorlevel 1 exit /b 1
)
echo cpu_model: %PROCESSOR_IDENTIFIER%> %OUT%.meta.txt
echo logical_cpus: %NUMBER_OF_PROCESSORS%>> %OUT%.meta.txt
echo preset: %PRESET%   runs: %RUNS%   rounds per run: %ROUNDS%   workers: %WORKERS%>> %OUT%.meta.txt

echo == warm-up (fills the file cache; not recorded)
%BIN%\kf_serial.exe --in %IN% >nul
if errorlevel 1 exit /b 1

for /l %%R in (1,1,%RUNS%) do (
    echo == run %%R / %RUNS%
    %BIN%\kf_serial.exe --in %IN% --repeat %ROUNDS% --csv %OUT% | findstr /c:"compute :"
    if errorlevel 2 exit /b 1
    for %%W in (%WORKERS%) do (
        echo    %%W workers:
        %BIN%\kf_master.exe --in %IN% --workers %%W --repeat %ROUNDS% --expect-file %REF% --csv %OUT% | findstr /c:"compute :"
    )
)
echo == done -^> %OUT%
echo    next:  python tools\plot_scaling.py --windows %OUT% --cores %NUMBER_OF_PROCESSORS%
exit /b 0
