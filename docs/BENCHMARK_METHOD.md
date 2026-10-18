# Benchmark method

This page explains how the numbers in `results/` are produced. It is written so the "Methodology" and "Threats to validity" sections of the report can be taken from it directly.

## What is measured

Every one of the 15 system calls is wrapped by a `plat_*` function (see `PLATFORM_API.md`). The benchmark times the wrapper, which on success does nothing but the call itself (no allocation, no logging). For each call it records:

| Quantity | How | Columns |
| --- | --- | --- |
| Time per call | `plat_now_ns()` (Linux `clock_gettime(CLOCK_MONOTONIC)`, Windows `QueryPerformanceCounter`) | `mean_ns`, `median_ns`, `p95_ns`, `stddev_ns`, `min_ns`, `max_ns` |
| CPU time | `plat_usage_self()` (Linux `getrusage`, Windows `GetProcessTimes`), read just before and just after each timed block and summed | `cpu_user_us`, `cpu_sys_us` (totals over all timed blocks; divide by `ops`) |
| Memory | `plat_usage_self()` peak resident set (Linux `ru_maxrss`, Windows `PeakWorkingSetSize`) | `peak_kb`, `peak_delta_kb` |

## Protocol for one case

A **case** is one call at one size, for example `plat_read` at 64 KB.

1. **Fresh process.** The driver starts a new copy of itself for every case (using `plat_spawn`). Peak memory is a high-water mark that never goes down, so only a fresh process lets each case report its own peak. It also stops one case from leaving cache, heap or kernel state behind for the next.
2. **Setup** (once): create the test files, buffers or segments the case needs.
3. **Warm-up:** 3 rounds (5 for process creation) are run and thrown away, so the first-use costs (page faults, dynamic linking, cache fill) are not in the result.
4. **Samples:** each sample is a *round* of `batch` calls:
   - `prepare` (untimed): get ready for `batch` calls, e.g. open `batch` files;
   - the **timed block**: `batch` calls of the wrapper, with one clock read before and one after;
   - `cleanup` (untimed): undo them, e.g. close the files.

   The sample's value is (time of the block) / `batch`.
5. **Statistics** over the samples: mean, median, 95th percentile (linear interpolation), sample standard deviation, min, max. These are unit-tested (`bench/test_stats.c`).
6. **Repeats:** the whole suite is run 5 times (`scripts/run_bench.*`), each run a separate set of processes. The analysis reports the **median of the 5 per-run medians** and shows the min-to-max range of those medians as a line behind each dot.

### Why batches

A clock read costs tens of nanoseconds (the `plat_now_ns` baseline case measures it on each machine). Timing a 100 ns call one by one would be mostly clock cost. Fast calls (tens of ns to a few µs) are therefore timed in batches of 100 to 100,000 so the clock cost is divided away. Slow calls (starting a process takes hundreds of µs) use `batch = 1`, where the clock cost is negligible. The loop overhead of the harness itself is measured by the `empty_call` baseline case (a few nanoseconds).

### Why the median

One background interrupt or a virus scan can make a single sample 100 times slower. The mean follows such outliers, the median does not. The mean, standard deviation and maximum are kept in the CSV, so the noise is visible, but charts and tables use the median.

## The cases

| Group | Call | What exactly is timed | Sizes |
| --- | --- | --- | --- |
| baseline | `empty_call` | harness loop only, no system call | - |
| | `plat_now_ns` | one clock read | - |
| | `plat_getpid` | `getpid()` on Linux; `GetCurrentProcessId()` on Windows, which does not enter the kernel | - |
| fileio | `plat_open` | open an existing file read-only | - |
| | `plat_open_create` | open with create + truncate (makes a new file) | - |
| | `plat_close` | close a file handle | - |
| | `plat_unlink` | delete an empty file (helper, not one of the 15) | - |
| | `plat_read` | sequential read, served from the file cache | 4 KB, 64 KB, 1 MB |
| | `plat_write` | sequential write into the file cache, never flushed | 4 KB, 64 KB, 1 MB |
| process | `plat_spawn` | start `child_helper`; returns when the child is running | - |
| | `plat_wait` | wait for a child that has already ended (call cost only) | - |
| | `plat_spawn_wait` | start a child and wait for it to exit | - |
| memory | `plat_map_anon`, `plat_map_file` | map only; no page touched | 4 KB, 1 MB, 16 MB |
| | `plat_map_anon_touch`, `plat_map_file_touch` | map, then write (anonymous) or read (file) one byte per 4 KB page | same |
| | `plat_unmap_anon`, `plat_unmap_file` | unmap an untouched mapping | same |
| | `plat_unmap_anon_touched`, `plat_unmap_file_touched` | unmap a mapping whose pages were all touched | same |
| ipc | `plat_pipe` | create a pipe | - |
| | `plat_pipe_write_read` | one write plus one read of `size` bytes in the same process | 64 B, 512 B, 2 KB |
| | `plat_shm_create` / `plat_shm_destroy` | create and map / detach and remove a named segment | 4 KB, 1 MB, 16 MB |
| | `plat_shm_attach` / `plat_shm_detach` | second mapping of an existing segment / remove one mapping | same |
| perms | `plat_chmod` | change the mode (alternates 0600 and 0644) | - |
| | `plat_chown` | set the owner to the current owner (a real call that changes nothing) | - |
| | `plat_stat_path` | read size, mode, owner (helper) | - |
| | `plat_umask` | set the creation mask | - |
| linux | `fork_wait`, `fork_wait_64mb` | raw `fork()` + child `_exit` + `waitpid`, with and without 64 MB of touched memory in the parent (Linux only) | - |

Three derived figures are computed by `bench/analyze.py` and need no extra case: read/write **throughput** (size / time), the **page-fault cost** ((map+touch) - map) / pages, and the **Windows / Linux ratio** of every call.

## Running it

```text
Linux / WSL:   bash scripts/build.sh
               bash scripts/run_bench.sh                  # 5 runs -> results/linux.csv
Windows:       scripts\build.bat
               scripts\run_bench.bat                      # 5 runs -> results\windows.csv
Either:        python bench/analyze.py results/linux.csv results/windows.csv --out results/plots
```

`--quick` (smoke test, also run by `ctest`) shrinks every case; its numbers must not be used. One full run took about 20 seconds in the development sandbox (Linux); note the time on your two machines in the report, Windows is expected to be slower because process creation costs more there.

A single case can be repeated by hand, for example `build/bin/bench --out /tmp/x.csv --filter plat_read --group fileio`.

## CSV columns

`os, run, group, call, size, batch, samples, mean_ns, median_ns, p95_ns, stddev_ns, min_ns, max_ns, ops, cpu_user_us, cpu_sys_us, peak_kb, peak_delta_kb`

- `size` is in bytes (0 when the call has no size); `ops = batch x samples`.
- `cpu_*` are the CPU time used inside the timed blocks only (the untimed prepare and cleanup are excluded, so for a call that never waits, CPU per call is close to the time per call). `peak_delta_kb` is the growth of the peak over the process's start-up peak.
- A `<file>.meta.txt` next to each CSV records the OS, CPU count, compiler and build type.

## Threats to validity (put these in the report)

1. **Linux numbers come from WSL2**, a lightweight virtual machine, not from bare-metal Linux. System calls that stay inside the guest kernel are close to native; anything that touches the disk or needs the hypervisor (page-fault handling, process creation) can cost more. Windows numbers are native. State this next to every comparison chart.
2. **Win32 API, not raw system calls.** Windows applications call `kernel32` functions, which call `ntdll`, which enters the kernel. The benchmark measures the Win32 call, which is what a program actually uses, but it is not the same layer as a Linux system call.
3. **Antivirus and indexing.** Windows Defender scans files as they are created and opened, which can inflate `plat_open`, `plat_open_create` and `plat_unlink`. Run on the same machine with the same settings, and say whether real-time protection was on. `plat_unlink` may retry up to 200 ms when a scanner holds the file; such samples show up as outliers (visible in `max_ns`).
4. **Warm cache only.** Reads come from the file cache and writes are never flushed. Cold-cache and flush costs depend on the disk, not the operating system's call path, and are not measured. (Dropping the Linux cache needs root; Windows has no simple equivalent.)
5. **CPU-time resolution.** Windows adds up CPU time in scheduler ticks of about 15.6 ms, so the CPU of a short case is rounding noise. The analysis shows Windows CPU per call only for cases that used at least 150 ms of CPU in total (otherwise "n/a"). Linux's total is exact to the microsecond and is always shown.
6. **Child processes are not in the CPU columns.** `getrusage(RUSAGE_SELF)` and `GetProcessTimes` count the calling process only, so the CPU a child uses while starting up is not included in the spawn cases. Wall time is.
7. **Power and thermal state.** A laptop on battery or hot after a long build runs slower and less evenly. Plug in, close other programs, and let the machine idle for a minute before a run. The 5 repeats and the spread line show how stable a run was.
8. **Different file systems.** Linux benchmarks must run on ext4 inside WSL (`~/os-ccp`), not on `/mnt/c`, which goes through a file-sharing bridge (the 9P protocol) and is far slower. `run_bench.sh` warns about this.
