# Evidence collected on windows

| ID | Requirement | Result | Detail | Proof |
|---|---|---|---|---|
| R1 | 15 Linux calls and their Windows equivalents are implemented | **PASS** | 15/15 Linux, 12/12 Windows found in the wrappers | results/evidence/windows/static_checks.txt; docs/SYSCALL_MAPPING.md |
| R2 | Each system call group has a demo that prints pass/FAIL | **PASS** | 5/5 demos passed | results/evidence/windows/demo_*.log |
| R3 | Errors of every wrapper are logged with the failing call, arguments and OS error text | **PASS** | 8 forced-error log lines; missing-file yes, bad-handle yes | results/evidence/windows/forced_errors.txt |
| R3b | Every wrapper that can fail has a logging call | **PASS** | 38/38 wrappers log | results/evidence/windows/static_checks.txt |
| R4 | File I/O, memory mapping and shared data go through the system-call wrappers (no fopen/fread/raw open/mmap) | **PASS** | no raw calls in kalman/kfio/worker/master/serial/gen; no malloc in the worker's or filter's data path (allocations: kf_master.c: 3, kf_serial.c: 1, kfgen.c: 1, kfio.c: 1) | results/evidence/windows/static_checks.txt |
| R5 | Portable code contains no OS-specific includes or #ifdef _WIN32 (platform.h boundary) | **PASS** | none outside src/linux, src/win and the two *_platform_* case files | results/evidence/windows/static_checks.txt |
| R6 | Parallel (1..8 workers) output is identical to serial, byte for byte; filter unit, statistical and pipeline tests pass | **PASS** | 100% tests passed out of 4 | results/evidence/windows/ctest.txt |
| R7 | Kalman output is correct: error well below the raw measurement error, and matches the filter's own prediction | **PASS** | CHECK PASSED | results/evidence/windows/kalman_check.txt |
| R8 | Execution time and resource use recorded per call for this OS; machine details stored with the data | **PASS** | results/windows.csv present, machine details present | results/windows.csv, results/windows.csv.meta.txt |
| R9 | Parallel scaling measured for this OS | **PASS** | results/scaling_windows.csv present | results/scaling_windows.csv |
| R10 | Design, method and environment are documented | **PASS** | 5/5 documents present | docs/ |
