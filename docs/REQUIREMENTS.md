# Requirements and the evidence for each

Every requirement of the assignment (CT-353 CCP) with the concrete file, log or chart that shows it is met.
Rows marked **auto** are re-checked by `python tools/collect_evidence.py` (run it on Linux and on Windows;
the proof is written to `results/evidence/<os>/`; its check id is in the first column).

## A. Constraints and deliverables of the assignment

| Id | Requirement | Evidence | How it is shown |
| --- | --- | --- | --- |
| R1 auto | Implement the 15 system calls of Table 1, on Linux and Windows | `src/linux/sys_linux.c`, `src/win/sys_win.c`; `docs/SYSCALL_MAPPING.md` (15 Linux calls, 12 Windows functions, the many-to-one pairings and why) | The tool looks for each underlying call in each wrapper file: 15/15 Linux, 12/12 Windows |
| R2 auto | Demonstrate each with a small program | `src/syscalls/demo_*.c` (file I/O, process, memory, IPC, permissions; `demo_process_linux` shows `fork`/`exec`/`wait` separately) | `results/evidence/<os>/demo_*.log`: one pass/FAIL line per check, final `DEMO <name> PASSED` |
| R3 auto | Error handling and logging for all system calls | `include/log.h`; every wrapper reports failure with the call, its arguments and the OS error text | `results/evidence/<os>/forced_errors.txt`: ERROR lines the demos provoke on purpose (missing file, invalid handle, double close, exec of a missing program, mmap of a bad handle, shmget of a missing segment, chmod of a missing file, wait with no child) |
| R3b auto | ... for *every* wrapper | `src/linux/sys_linux.c`, `src/win/sys_win.c` | The tool lists any wrapper that can fail but has no logging call (result: none) |
| R4 auto | File I/O and memory operations use system calls | `src/kalman/kfio.c` (all file access via `plat_open/read/write/map_file`), `kf_worker.c`, `kf_master.c` | The tool scans for `fopen/fread/fwrite` and raw `open/read/write/mmap/CreateFile` in the Kalman programs (none) and for `malloc` in the filter and worker (none). The few `malloc`s that exist are for bookkeeping arrays and the optional `--io read` comparison mode; the CSV timing rows use `fopen` because they are measurements, not sensor data (stated in `docs/KALMAN_DESIGN.md`) |
| R5 auto | Cross-platform, modular, maintainable | `include/platform.h` is the only OS boundary; `src/linux/`, `src/win/` implement it; `docs/PLATFORM_API.md` | The tool scans portable code for `_WIN32`, `__linux__`, `<windows.h>`, `<unistd.h>`, `<sys/mman.h>`... (none outside the platform layer and the two OS-specific benchmark case files) |
| R6 auto | Parallelise the Kalman filter with processes and IPC | `src/kalman/kf_master.c`, `kf_worker.c`, `kfproto.c`; design and data flow in `docs/KALMAN_DESIGN.md` ("Parallel version") | `kf_parallel` test: 1, 2, 3, 5, 8 workers over 11 tracks give the same hash and a byte-identical file as the serial program; killed workers leave no stray process or segment; `results/evidence/<os>/ctest.txt` |
| R7 auto | Correct Kalman output | `src/kalman/kalman.c`; `docs/KALMAN_DESIGN.md` (validation table) | Position error 7.07 m (raw) -> 1.48 m (filtered), predicted 1.48 m (ratio 0.998), 4.79x reduction; five test layers incl. an independent Python implementation; `results/evidence/<os>/kalman_check.txt` |
| R8 auto | Execution time and resource use for each system call, both OS | `results/linux.csv`, `results/windows.csv` (+ `.meta.txt` with the machine), `results/plots/summary.md`, `*_compare.png`, `ratio_windows_over_linux.png`; method in `docs/BENCHMARK_METHOD.md` | Columns: wall time (median, p95, stddev, min, max), CPU user/system, peak memory; 5 repeats, 610 rows, 0 failures |
| R9 auto | Compare performance between the two OS; document differences | `results/plots/summary.md`; `docs/ANALYSIS.md` (hypotheses with tests); `results/scaling_*.csv` and `results/plots/scaling_*.png` for the Kalman run | Each large gap has a hypothesis, a test and a status in `docs/ANALYSIS.md`; unsupported explanations are labelled as hypotheses |
| R10 auto | Performance metrics documented and reproducible | `docs/ENVIRONMENT.md` (versions, hardware), `docs/BENCHMARK_METHOD.md`, seeds in the data header, one script per OS (`scripts/run_bench`, `make_data`, `run_scaling`) | A clean checkout builds and runs with the scripts; data files are regenerated from the seed and checked by hash |
| R11 | Report | `report/` (Phase 8): description of the system calls, Kalman implementation, tables and charts, lessons | Built from `docs/` and `results/plots/` |
| R12 | Presentation of at most 12 slides | `slides/` (Phase 9) | Built from the report charts |

## B. How the rubric criteria are answered

| Criterion | Where |
| --- | --- |
| Clarity of objective | This table (every requirement identified and mapped); `docs/SYSCALL_MAPPING.md` (where the assignment's table does not map one-to-one: 15 Linux calls vs 12 Windows functions, `fork`/`exec`, `umask`, file mapping needed in addition to `VirtualAlloc`) |
| Innovation / depth of analysis | Parallelism across tracks rather than across time, with the reason (a Kalman filter step needs the previous step); correctness gate = hash equality with serial; the hardware-ceiling control experiment (`tools/run_control.py`) that separates the machine's limit from the design's; Karp-Flatt analysis; hypothesis register `docs/ANALYSIS.md` |
| Modularity | `platform.h` boundary; separate demo programs; benchmark harness; generator, serial filter, master and worker as separate programs sharing `kalman.c`/`kfio.c`/`kfproto.c`; the filter code contains no system calls |
| Validation | Parallel = serial (byte-identical); five layers of filter validation; `tools/collect_evidence.py`; forced-failure logs; run-to-run spreads and the "no measurable difference" rule |
| Planning | Project plan (nine phases, risks and fallbacks); actual days spent to be recorded in the report |

## C. Known limitations (stated, not hidden)

- Linux is measured inside WSL2 (a VM); Windows natively. Neither is "bare metal Linux".
- Windows wrappers call Win32 functions that may perform several kernel operations; Linux wrappers call the raw system call. The comparison is of the two *APIs*.
- Windows `plat_chmod`/`plat_chown` translate between mode bits and ACLs; their timings measure that translation layer as much as the OS.
- Warm page cache only; antivirus state recorded in `report/notes.md`.
- Windows CPU time is counted in 15.6 ms ticks, so per-call CPU figures are shown only above 150 ms of total CPU.
- Speedup is specific to this 4-core / 8-thread laptop.
