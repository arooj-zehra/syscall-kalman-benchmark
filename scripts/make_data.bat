@echo off
rem Generate a data set, run the serial filter on it and check the result (Windows).
rem Usage: scripts\make_data.bat [small|medium|large]      (default: medium)
rem   small   16 tracks x    2,000 steps  (0.5 MiB)
rem   medium 128 tracks x   20,000 steps  (39 MiB)
rem   large  256 tracks x  100,000 steps  (391 MiB input, 781 MiB output)
rem Writes data\meas_<preset>.bin and data\est_serial_<preset>.bin (the serial result the
rem parallel runs are checked against). Timing comes from scripts\run_scaling.bat.
rem The data files are not committed (see .gitignore).
setlocal
cd /d "%~dp0\.."

set "PRESET=%~1"
if "%PRESET%"=="" set "PRESET=medium"
set "BIN=build\bin"
if not exist %BIN%\kfgen.exe (
    echo ERROR: build first:  scripts\build.bat
    exit /b 1
)
if not exist data mkdir data
if not exist results mkdir results

echo == generate (%PRESET%)
%BIN%\kfgen.exe --preset %PRESET% --out data\meas_%PRESET%.bin
if errorlevel 1 exit /b 1

echo == serial filter
%BIN%\kf_serial.exe --in data\meas_%PRESET%.bin --out data\est_serial_%PRESET%.bin --io mmap
if errorlevel 1 exit /b 1

echo == evaluate
%BIN%\kf_eval.exe --meas data\meas_%PRESET%.bin --est data\est_serial_%PRESET%.bin --check
exit /b %errorlevel%
