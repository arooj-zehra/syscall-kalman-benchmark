# Syscall Kalman Benchmark

CT-353 Operating Systems, Complex Computing Problem (CCP).

An operating-systems project that implements 15 Linux system calls and their Windows equivalents, benchmarks them on both systems, and uses them to build a **multi-process Kalman filter**: sensor data is read through file I/O, shared through memory mapping and shared memory, filtered by worker processes, and written back to a file.

**Author:** _your name, roll number_ (solo project)

## What the project delivers

| Deliverable | Where it lives |
| --- | --- |
| 15 system calls with demo programs (Linux and Windows) | `src/syscalls/` |
| Benchmark harness (time, CPU, memory per call, CSV output) | `bench/` |
| Serial and multi-process Kalman filter | `src/kalman/` |
| Linux versus Windows comparison, charts | `results/`, `bench/` scripts |
| Report and presentation | `report/` |

## Design in one minute

- **One C11 codebase, one boundary.** `include/platform.h` declares everything the portable code needs from the OS. `src/linux/platform_linux.c` and `src/win/platform_win.c` implement it. Code outside those two folders never includes `<windows.h>` and never uses `#ifdef _WIN32`.
- **Every system call goes through a wrapper** that checks the result and logs the OS error (`errno` on Linux, `GetLastError()` on Windows) with `LOG_SYSERR(...)`.
- **The Kalman recursion is sequential in time**, so parallelism is across tracks: each worker process owns a slice of the tracks, reads input from a memory-mapped file and writes into shared memory. Workers are separate executables started with `fork`+`exec` (Linux) or `CreateProcess` (Windows).
- Details: [docs/SYSCALL_MAPPING.md](docs/SYSCALL_MAPPING.md) for the Linux-to-Windows pairing, [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md) for the test machine.

## Repository layout

```
include/        platform.h (OS boundary), log.h (logging)
src/common/     portable code (log.c now; Kalman math and wrappers later)
src/linux/      Linux implementation of platform.h
src/win/        Windows implementation of platform.h
src/syscalls/   Phases 2-3: demo program per call group
src/kalman/     Phases 5-6: data generator, serial filter, master, workers
bench/          Phase 4: benchmark harness and chart scripts
data/           generated sensor files (not committed)
results/        benchmark CSVs and charts
report/         report notes and final report
docs/           environment record, syscall mapping
scripts/        build.sh (Linux/WSL), build.bat (Windows)
src/hello.c     Phase 1 smoke test
```

## Requirements

| Tool | Windows (cmd) | Linux (WSL2 Ubuntu) |
| --- | --- | --- |
| C compiler | GCC from MSYS2 MinGW-w64 | GCC |
| CMake | 3.20 or newer | 3.20 or newer |
| Build tool | Ninja or `mingw32-make` | Ninja or make |
| Python 3 | with matplotlib, pandas | with matplotlib, pandas |

## Setup: two clones of the same repo

The project is built natively on each OS, so each OS needs its own clone. Keep the Linux copy **inside the Linux filesystem** (`~/os-ccp`), not under `/mnt/c/...`; file access across that boundary is much slower and would distort the Linux file I/O benchmarks.

**Windows (cmd):**

```bat
cd C:\Users\Kazim\os-ccp
git clone https://github.com/arooj-zehra/syscall-kalman-benchmark .
scripts\build.bat
```

**Linux (WSL):**

```bash
cd ~/os-ccp
git clone https://github.com/arooj-zehra/syscall-kalman-benchmark .
bash scripts/build.sh
```

(`git clone ... .` works because the folders are empty.)

Both scripts configure with CMake, build in `build/`, run `ctest`, and run the smoke test. A passing run ends with `SMOKE TEST PASSED`.

To build by hand: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` then `cmake --build build`. Executables land in `build/bin/`.

### Windows notes

- If `build.bat` says it cannot find a build tool, install Ninja in the MSYS2 shell that matches your compiler: `pacman -S mingw-w64-ucrt-x86_64-ninja` (UCRT64) or `pacman -S mingw-w64-x86_64-ninja` (MINGW64).
- Executables are linked statically, so they run from plain `cmd` without MSYS2 on `PATH`.
- MinGW has no `fork()`. That is expected: the Windows side uses `CreateProcess` (see the mapping document).

### Day-to-day Git flow

Edit and commit on whichever OS you are working on, `git push`, then `git pull` on the other. Line endings are controlled by `.gitattributes`, so scripts stay LF for WSL and `.bat` files stay CRLF for cmd.

## Conventions

- **Language:** C11, warnings on (`-Wall -Wextra -Wpedantic`). The build must stay warning-free.
- **Logging:** use `LOG_INFO/WARN/ERROR`; after a failing system call use `LOG_SYSERR("call(args)")` before anything else can overwrite the error code.
- **Timing:** `plat_now_ns()` is the only clock the benchmarks use.
- **Benchmark builds** are `Release` on both systems with the same optimisation level.
- **Reproducibility:** fixed random seeds, machine details recorded in `docs/ENVIRONMENT.md`, results stored as CSV under `results/`.

## Progress (22 working days)

- [x] **Phase 1 (day 1):** toolchains, repo, folder layout, `platform.h`, `log.h`, build on both OSes
- [ ] **Phase 2 (days 2-4):** the 15 Linux calls, one demo per group
- [ ] **Phase 3 (days 5-7):** Windows equivalents and the mapping document
- [ ] **Phase 4 (days 8-9):** benchmark harness, CSV output, chart script
- [ ] **Phase 5 (days 10-11):** data generator and serial Kalman filter
- [ ] **Phase 6 (days 12-15):** multi-process Kalman filter with system calls
- [ ] **Phase 7 (days 16-17):** comparison, hypotheses, requirement checks
- [ ] **Phase 8 (days 18-20):** report
- [ ] **Phase 9 (days 21-22):** presentation and final submission

The full plan with tasks and "done when" checks is in the project plan document.

## Phase 1 acceptance check

Phase 1 is done when, on **both** operating systems:

1. `scripts/build.sh` / `scripts\build.bat` builds with no warnings and ends with `SMOKE TEST PASSED`.
2. The log contains an `os:` line, a `compiler:` line, and the expected `selftest ... errno 2` / `error 3` line.
3. The `os:` line is pasted into `docs/ENVIRONMENT.md`.
4. Everything is committed and pushed, and a fresh clone builds.
