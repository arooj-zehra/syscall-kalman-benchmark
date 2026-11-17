# Analysis: what the numbers say, hypotheses and how each was (or will be) tested

Machine for every result: Intel Core i7-7700HQ (4 cores, 8 threads), 16 GB RAM, SSD. **Linux = Ubuntu in WSL2** (a virtual machine on the same Windows 10 host), **Windows = native Windows 10, MinGW-w64 GCC 14.2**. Linux: GCC 15.2. Both Release builds, 5 repeats (benchmark) and 5 sweeps (scaling). A "difference" is only claimed when the min..max ranges of the repeats do not overlap.

Status key: **Supported** = the data in hand backs it; **Test ready** = a command below decides it and has not been run yet; **Open** = no decisive test within this project.

## Part 1. The Kalman speedup (Phase 6 data, large preset: 256 tracks x 100,000 steps)

| | Linux (WSL2) | Windows |
| --- | --- | --- |
| Serial compute | 2554 ms | 2932 ms (1.15x Linux) |
| Best parallel compute | 739 ms (7 workers) | 837 ms (7 or 16 workers) |
| Best speedup | **3.45x** (efficiency 0.49) | **3.47x** (efficiency 0.50 at 7) |
| Speedup with 4 workers (= physical cores) | 2.68x | 2.60x |
| Speedup with 8 workers (= logical cores) | 3.24x | 3.43x |
| Beyond 8 workers | flat (3.3-3.4x), no gain | flat (3.4-3.5x), no gain |
| Start-up, 1 / 16 workers | 1.3 / 10.8 ms | 11.5 / 90.0 ms |
| Total CPU time, serial -> 8 workers | 2784 -> 6318 ms (2.3x) | 3010 -> 5932 ms (2.0x) |

Correctness comes first: every point above is hash-verified against the serial result (same `est_hash` 0x07a6e04a7243bf76 on both systems), so the speedups compare identical outputs.

### What the data supports directly

**S1. Speedup stops at about 3.4-3.5x (0.85 per physical core), and more processes do not help.** Going from 7 to 8 to 12 to 16 workers changes the speedup by less than the spread (Linux 8 workers: 2.60-3.58x; 7 workers: 3.20-3.88x - the ranges overlap), so the small dips at 6 and 8 workers on Linux are noise, not an effect. Beyond the 8 hardware threads the extra processes only wait for a CPU.

**S2. The loss is not an Amdahl serial fraction.** The Karp-Flatt metric (the fraction of the run that behaved as if serial) is not constant: Linux 0.12 (2 workers) -> 0.17 (7) -> 0.255 (16); Windows 0.16 -> 0.17 -> 0.24. A real serial part would give a flat line. By construction there is no serial code inside the timed region (the round starts when every worker has its block and ends when the last one reports), so what grows with the worker count is *contention for shared hardware*, not unparallelised code.

**S3. Total CPU time rises by 2x while the job is the same.** With 8 workers the processes burn 2.0-2.3x the CPU seconds of the serial run. The extra CPU time is not extra work (the hash proves identical work); it is time that threads sit on a core they share with another thread (hyper-threading) or run at a lower clock. This is the signature of S2's contention. (Windows CPU times are counted in 15.6 ms ticks; over runs of 5-6 s that is accurate to about 2 %.)

**S4. Shared memory and mmap do what they were chosen for: memory does not grow with the worker count.** Peak resident memory per worker is 1173, 588, 395, 295, ... 75 MiB for 1, 2, 3, 4, ... 16 workers on Linux (Windows: 1175 ... 77): `peak x workers` stays at about 1.17-1.23 GiB = the serial program's 391 MiB input + 781 MiB output. Nothing is copied per worker; each touches only its tracks of the one shared input and the one shared output.

**S5. Process start-up is a real cost on Windows, small on Linux.** Start-up grows linearly with the workers: about 0.63 ms per worker on Linux and 5.2 ms on Windows (8x). At 16 workers that is 1.4 % of the run on Linux but 10.8 % on Windows (90 ms of 837 ms). This matches the Phase 4 process benchmark: `plat_spawn` 0.55 ms (Linux) vs 1.56 ms (Windows), `plat_spawn_wait` 1.09 ms vs 7.56 ms. On Windows the new process also has to load its image and, probably, be scanned (see H6), and the master waits for that (READY barrier), so the child's start-up lands on the critical path.

**S6. Memory was not the limit, and neither was hashing.** The filter touches a few dozen doubles per step, so the run is compute-bound: about 1.3 GB (391 MiB read + 781 MiB written) moves through memory per round in ~0.75 s, roughly 1.6 GB/s, a small part of what this machine's DDR4 can deliver. Hashing the result (169 ms, one process) is 6 % of the serial run; it caps the *end-to-end* speedup (Linux 2.88x vs 3.41x for compute alone at 12 workers) but is not what limits the compute speedup. A running hash chain cannot be split across workers without changing its definition; hashing per track and combining would remove that cost, at the price of a less simple check (not done; verification, not computation).

**S7. Linux runs vary more than Windows.** Min..max range of the speedup at 8 workers: Linux 2.60-3.58x (spread 0.98), Windows 3.35-3.65x (0.30). Same hardware, same program. Consistent with a virtual machine whose virtual CPUs are scheduled by the host (H5).

**S8. The parallel program with one worker is as fast as the serial one** (efficiency 1.01 Linux, 1.03 Windows). So the IPC machinery (pipes, shared segment, extra process) costs nothing measurable per round. (The Windows 1-worker range, 1.01-1.07, lies slightly above 1; the cause is not identified and the effect is at most 7 %.)

### Hypotheses (each with a test)

| # | Observation | Hypothesis | Test | Status |
| --- | --- | --- | --- | --- |
| **H1** | Speedup plateaus at ~3.4x on 4 cores / 8 threads; efficiency 0.89 already at 2 workers | The plateau is the hardware's limit, not the design's: hyper-threads share one core's execution units (typically +20-30 % for two threads), the clock falls as more cores are busy (laptop, turbo), L3 and memory are shared | **Control experiment**: run N *independent* `kf_serial` processes (no shared memory, pipes or master) and compare their combined throughput with `kf_master` on the same data. If independent processes also reach only ~3.4-3.6x, the design loses nothing the hardware would not take anyway. If they reach much more, the design (page faults on the shared segment, pipe round trips, write-allocate traffic) is guilty and H1 is refuted. `python tools/run_control.py`, then `tools/plot_control.py` | Test ready |
| **H2** | Same serial program is 15 % slower on Windows (2932 vs 2554 ms; Phase 5: 312 vs 271 ms) | The gap is not the OS (the timed loop makes no system calls). Candidates: compiler (GCC 14.2 MinGW vs 15.2), CPU frequency / power plan (VM on the Windows host vs the Windows power plan "Balanced"), code alignment | (a) Windows power plan "High performance", plugged in, rerun `kf_serial --repeat 5`; (b) same GCC major version on both (`pacman -S` the newer MinGW GCC if offered); (c) compare `kf_serial` run on Linux built with the same `-O2`-class flags (CMake shows them). Only claim a cause if the gap closes | Test ready |
| **H3** | The best Windows speedup (3.47x) equals Linux's (3.45x) although Windows is slower in absolute terms | Speedup is a ratio against each system's own serial run, so a constant factor (H2) cancels; the *absolute* best time is what differs (739 vs 837 ms = 1.13x) | Arithmetic from the table; no experiment needed. The report states both numbers and never compares speedups across systems without the absolute times | Supported |
| **H4** | Start-up per worker 5.2 ms on Windows vs 0.63 ms on Linux | Windows `CreateProcess` is heavier than `fork`+`exec` (image loading, process object set-up, no copy-on-write shortcut), and antivirus scans the freshly started executable | `kf_master --workers 16` with real-time protection state noted; then exclude `build\bin` in Windows Security (Add-MpPreference -ExclusionPath) and compare `startup_ms` over 5 runs with `tools/compare_runs.py`-style medians. If start-up falls clearly, scanning is part of it; whatever remains is `CreateProcess` | Test ready |
| **H5** | Linux (WSL2) speedup spread is ~3x Windows' | The VM's virtual CPUs are time-sliced by the host scheduler, which adds jitter that a native process does not see | Compare spreads of the independent-process control experiment on both systems (same effect expected); `.wslconfig` `processors=8` is already the default. Not testable further without changing the setup, so the report states "consistent with", not "caused by" | Open (partly testable) |
| **H6** | Windows `plat_open` 27 us vs 2.5 us (11x), create 250 vs 12 us (22x), unlink 191 vs 8.6 us (22x) - much more than read/write (1.1-3.4x) | The cost is in path resolution and security/filter-driver processing of each open (NTFS, Windows Defender real-time scanning on file open/create), not in the data transfer; Linux's ext4 inside WSL2 also has the page cache closer to the process | Run the `fileio` group three times with Defender's real-time protection **on**, then three times with the temp folder **excluded** (admin PowerShell: `Add-MpPreference -ExclusionPath <full path of results\tmp>`; remove the exclusion afterwards), compare with `tools/compare_runs.py --group fileio`. A drop in open/create/unlink and no change in read/write confirms the hypothesis. Note: Linux numbers are ext4-in-a-VM, Windows numbers NTFS, so even a confirmed result is not "Linux kernel vs Windows kernel" | Test ready |
| **H7** | Linux first-touch of anonymous memory is 4x slower than Windows (1 MB: 610 vs 159 us; 16 MB: 9.56 vs 2.17 ms); but 1 page: 4.8 vs 3.9 us (no gap) | Linux (inside WSL2) pays a full page-fault exception per 4 KB page - made more costly by the VM (two-level address translation, mitigations: the null system call alone costs 0.9 us) - while Windows' memory manager zeroes pages ahead of time and handles the fault more cheaply per page in bulk | The gap *grows with size* (1x at 4 KB, 4x at 1 MB), pointing at per-page cost. Test: (a) `cat /sys/kernel/mm/transparent_hugepage/enabled`; with huge pages the per-4 KB fault count drops 512x; (b) a variant using `MAP_POPULATE` pre-faults in one call. If both shrink the gap, the cost is the per-page fault path, not the allocation call | Test ready (needs a small variant of the memory case) |
| **H8** | Windows first-touch of a *file* mapping is 12-13x slower than Linux (1 MB: 476 vs 40 us; 16 MB: 8.1 ms vs 614 us) | Linux's fault-around maps up to 16 neighbouring page-cache pages per fault; the Windows memory manager services file-backed faults through the cache manager with more work per page | Linux: `/sys/kernel/debug/mm/fault_around_bytes` (needs root) set to 4096 should slow Linux by about 16x if fault-around is the cause. Relevance to the project: the Kalman input is a file mapping, but its first-touch cost lands in the first round only (`compute_first_ms`), which the median over rounds excludes | Test ready (optional) |
| **H9** | `plat_getpid` costs 903 ns on Linux, 5 ns on Windows | (a) Windows `GetCurrentProcessId` reads a field of the thread environment block and never leaves user mode, so it is not a system call at all; (b) the 0.9 us Linux figure is a high system-call entry cost specific to this platform (WSL2 on a Kaby Lake CPU, possibly with kernel page-table isolation or other mitigations) | (a) follows from the API and is why Windows `plat_getpid` and `plat_umask` are excluded from the ratio chart (marked n/a); (b) the same call costs ~0.11 us in my development sandbox (another virtualised environment), so 0.9 us is not a property of the call; the cause on this machine could be checked with `dmesg`/`/sys/devices/system/cpu/vulnerabilities/` | (a) Supported, (b) Open |

### Claims the data does not allow

- "Linux is faster/slower than Windows." The comparison is *Linux inside a VM, using a raw system call* against *native Windows, using a Win32 function that may do several kernel calls*. The report phrases every result as this pair, never as "the" Linux or "the" Windows.
- Any explanation in the table above marked Test ready or Open is a hypothesis until its test is run. The report lists it as such.
- One machine, one CPU: the numbers (especially the ~3.4x ceiling) are properties of this laptop; the *method* transfers, the numbers do not.

## Part 2. Where each statement comes from

| Statement | File |
| --- | --- |
| Scaling numbers, speedup, Karp-Flatt, CPU, memory, start-up | `results/plots/scaling_summary.md`, `scaling_summary.csv`, `scaling_speedup.png`, `scaling_breakdown.png` |
| Raw rows behind them | `results/scaling_linux.csv`, `results/scaling_windows.csv` (+ `.meta.txt`) |
| System-call timings and ratios | `results/plots/summary.md`, `summary.csv`, `*_compare.png`, `ratio_windows_over_linux.png` |
| Hardware ceiling for H1 | `results/control_<os>.csv`, `results/plots/control_scaling.png` |
| Defender test for H4 / H6 | `results/hyp_*.csv` and the `compare_runs.py` output pasted into `report/notes.md` |

## Part 3. Experiments still to run (about 40 minutes)

On **each** OS, from the repo root, after pulling the Phase 7 overlay and building:

```text
# H1: hardware ceiling (medium preset, about 5 minutes)
python tools/run_control.py                       # writes results/control_<os>.csv, prints a table
python tools/plot_control.py --linux results/control_linux.csv --windows results/control_windows.csv --cores 8 --physical 4

# Requirement evidence (about 1 minute)
python tools/collect_evidence.py                  # writes results/evidence/<os>/

# H6 on Windows only (admin PowerShell for the exclusion lines)
build\bin\bench.exe --group fileio --out results\hyp_defender_on.csv  --run 1 --tmpdir results\tmp     # repeat with --run 2 and 3
Add-MpPreference -ExclusionPath "$PWD\results\tmp"
build\bin\bench.exe --group fileio --out results\hyp_defender_off.csv --run 1 --tmpdir results\tmp     # repeat with --run 2 and 3
Remove-MpPreference -ExclusionPath "$PWD\results\tmp"
python tools/compare_runs.py results\hyp_defender_on.csv results\hyp_defender_off.csv --a-name "Defender on" --b-name "excluded" --group fileio
```

H2, H4, H7 and H8 are optional extensions; run them if time allows, otherwise the report lists them as hypotheses with the test that would decide them. Note in `report/notes.md` whether Defender real-time protection was on during the benchmark runs.
