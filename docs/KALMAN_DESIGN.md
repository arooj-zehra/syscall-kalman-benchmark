# Kalman filter: model, data, file format, validation

Phase 5 delivered the data generator and the serial filter; Phase 6 runs the same filter in several processes (last sections). This page also fixes everything that must stay identical between the two.

## What the program does

A Kalman filter estimates the position and velocity of a moving target from noisy position readings. We track many independent targets ("tracks"). Each track is a long sequence of readings; the filter processes it step by step, because every estimate depends on the one before. Tracks do not depend on each other, which is what makes the problem parallel (Phase 6): the work is split **across tracks**, never inside one.

## Model

State `x = [px, py, vx, vy]` (metres, metres per second), time step `dt`.

| Part | Definition |
| --- | --- |
| Motion | `x(k) = F x(k-1) + w`, `F = [[1,0,dt,0],[0,1,0,dt],[0,0,1,0],[0,0,0,1]]` |
| Process noise | white-noise acceleration, standard deviation `sigma_a`; per axis `Q = sigma_a^2 * [[dt^4/4, dt^3/2],[dt^3/2, dt^2]]` on (position, velocity) |
| Measurement | `z(k) = H x(k) + v`, `H = [[1,0,0,0],[0,1,0,0]]`, `v ~ N(0, sigma_r^2 I)` |
| Prior | `x = 0`, `P = diag(sigma_p0^2, sigma_p0^2, sigma_v0^2, sigma_v0^2)` before the first reading |

Default parameters: `dt = 0.1 s`, `sigma_a = 0.5 m/s^2`, `sigma_r = 5 m`, `sigma_p0 = 1000 m`, `sigma_v0 = 10 m/s`.

Each step: **predict** (`x = F x`, `P = F P F' + Q`, skipped for the first reading), then **update**:

```
S = H P H' + R            innovation covariance (2x2)
K = P H' S^-1             gain (4x2)
x = x + K (z - H x)
P = (I - K H) P (I - K H)' + K R K'     Joseph form, then symmetrised
```

The Joseph form costs a little more than `(I - K H) P` but keeps `P` symmetric and positive definite over millions of steps; a test runs 100,000 steps and checks both.

The generator draws the truth from exactly this model (including the prior), so the filter is the optimal estimator for the data and its error can be predicted in advance. That prediction is a validation tool (below).

## Code layout (`src/kalman/`)

| File | Contents | OS code? |
| --- | --- | --- |
| `kalman.c/.h` | filter: model setup, predict, update, `kf_run_track` | none |
| `datagen.c/.h` | random numbers and track generation | none |
| `kfio.c/.h` | file format, hash, reader and writer | only through `plat_*` |
| `kfgen.c` | writes a measurement file | via `kfio` |
| `kfproto.c/.h` | control messages, work division, sibling-executable path, timing record and CSV | only through `plat_*` |
| `kf_serial.c` | serial reference filter | via `kfio` |
| `kf_master.c`, `kf_worker.c` | parallel filter (Phase 6) | only through `plat_*` |
| `kf_eval.c` | error of a result file against the regenerated truth | via `kfio` |
| `test_kalman.c`, `kf_check.c`, `pipeline_test.cmake`, `parallel_test.cmake` | tests | |

`kalman.c` and `datagen.c` contain no system calls and no allocation, so Phase 6 workers call them unchanged. Everything that touches files goes through the wrappers of Phases 2 to 3 (`plat_open`, `plat_write`, `plat_read`, `plat_map_file`, ...): the generator and serial filter already exercise file I/O and memory mapping on both operating systems.

## Synthetic data

One random stream per track, derived from `(seed, track)`, so a track can be regenerated alone, in any order, by any process.

| Piece | Choice | Why |
| --- | --- | --- |
| Bits | xoshiro256**, seeded through splitmix64 (track number mixed in multiplicatively so neighbouring tracks do not overlap) | fast, passes standard statistical tests, 20 lines |
| Uniform | top 53 bits times 2^-53 | exact in a double |
| Normal | sum of 12 uniforms minus 6 (Irwin-Hall) | mean 0, variance exactly 1, uses only `+`, so **no libm call**: Linux and Windows produce the same bits. The tails stop at +-6 sigma, irrelevant here |
| Truth | start from the prior; each step draw acceleration `a ~ N(0, sigma_a^2)` per axis: `p += v dt + a dt^2/2`, `v += a dt` | the model the filter assumes |
| Measurement | `z = p + N(0, sigma_r^2)` per axis | |

The draw order per track is fixed: initial `px, py, vx, vy`; then for every step `k > 0` the accelerations `ax, ay`; then the measurement noises `x, y`. `tools/kf_reference.py` mirrors this order exactly, which is how it can check the C code bit for bit. Compile with `-ffp-contract=off` (set in CMake) so no compiler fuses a multiply and an add.

### Presets

| Preset | Tracks x steps | Input | Output | Serial compute (development sandbox, Linux) |
| --- | --- | --- | --- | --- |
| small | 16 x 2,000 | 0.5 MiB | 1 MiB | 5 ms |
| medium | 128 x 20,000 | 39 MiB | 78 MiB | 0.24 s |
| large | 256 x 100,000 | 391 MiB | 781 MiB | 2.3 s (plus 0.9 s to write the output) |

About 10 million filter steps per second per core on that machine; expect different figures on yours. `large` is the one to use for the Phase 6 speedup measurements: it is long enough that process start-up is small next to the work. (Sizes are in MiB, 1 MiB = 1,048,576 bytes, as `kfgen` prints them.)

## File format

`kfgen` writes measurement files, `kf_serial` and `kf_master` write estimate files; both use one format (`src/kalman/kfio.h`):

```
bytes 0 .. 4095     header, one page: magic, version, values per step, tracks, steps,
                    seed, dt, sigma_a, sigma_r, sigma_p0, sigma_v0, data hash
bytes 4096 ..       track 0 (all steps), track 1, ...; per step: 2 doubles (measurement)
                    or 4 doubles (estimate), little-endian
```

- **Self-describing:** the model parameters are in the header, so no side file is needed.
- **Page-aligned data:** a memory-mapped file is used in place, with no copying.
- **Track-major:** each track is contiguous, so a worker can take a block of tracks and write its block of the shared output without touching anyone else's.
- **Hash:** FNV-1a over 64-bit words of the data region. Two files with the same hash have (with overwhelming probability) the same data; comparing hashes replaces comparing 781 MiB. The writer patches the hash into the header at the end through a writable file mapping.

## Validation

Layers, from cheap to strong. Each catches things the others cannot; four deliberately injected bugs (a 10 % error in Q, a dropped term in the update, 11 instead of 12 uniforms in the normal, measurement noise drawn in the wrong order) were each caught by at least two layers.

| Layer | Test | What it proves |
| --- | --- | --- |
| Known answers | `test_kalman`: splitmix64 against its published outputs; hand-computed `Q`, one predict and one update step; hash of three known doubles | the basic building blocks are right |
| Behaviour | noise-free straight line is tracked exactly; `P` stays symmetric and positive definite for 100,000 steps; uniform and normal moments | the filter and generator behave as theory says |
| Determinism | same seed gives the same bytes; a track regenerated alone equals the same track in a file; a different seed changes every value; the pipeline test compares two runs byte for byte | results are reproducible, tracks are independent |
| File I/O | write, read with `plat_read`, read with `mmap`: identical data and hash | the format and both read paths work |
| Statistical consistency | `kf_check`: mean NEES = 4 (4 states), mean NIS = 2 (2 measurements), measurement noise equals `sigma_r`, filter error equals the error predicted by `P` | filter and data agree with each other. This is how a Kalman filter is validated when the truth is known |
| Independent implementation | `tools/kf_reference.py`: generator mirrored in Python must match the C bytes **exactly**; a textbook filter (standard update, matrix inverse) must match the C estimates to 1e-6 (measured: 1.7e-11 on values near 1,400) | the C code does what the equations say, written a second time in a different language and form |
| Cross-platform | the Windows build, run under Wine, produced measurement and estimate files byte-identical to Linux's (medium preset) | the same data and results on both operating systems. Repeat on real Windows by comparing `data_hash` and `est_hash` |

On the medium data set `kf_eval` reports: raw measurement error 7.07 m (2-D, `sigma_r * sqrt(2)`), filter error 1.476 m against 1.479 m predicted (ratio 0.998), velocity error 0.470 m/s against 0.470 m/s predicted: a 4.8-fold noise reduction.

What the statistical test cannot see: a 10 % error in Q still passes `kf_check`, because the filter is not very sensitive to it. The hand-computed and reference checks catch that, which is why the layers are all needed.

## Commands

```text
bash scripts/build.sh                  scripts\build.bat                 build and run all tests
bash scripts/make_data.sh medium       scripts\make_data.bat medium      generate, filter, evaluate
build/bin/kf_serial --in data/meas_large.bin --io read      load with plat_read instead of mmap
python tools/kf_reference.py check --meas data/meas_small.bin --est data/est_serial_small.bin
python tools/plot_track.py --meas data/meas_medium.bin --est data/est_serial_medium.bin
```

`ctest --test-dir build` gains four tests: `kf_unit`, `kf_consistency`, `kf_pipeline` (skips the Python reference if numpy is not installed) and `kf_parallel` (Phase 6).

For timing runs of `kf_serial`, leave out `--verify-input`: its hash pass over the whole file would be counted as load time. Phase 6 changed `kf_serial` to compute into one output buffer, hash it and then write (as the parallel master does), and to use the shared CSV layout, so Phase 5 serial timings are not comparable with later ones; `make_data` no longer writes timing rows, `run_scaling` does.

## Parallel version (`kf_master` + `kf_worker`)

### Data flow

```
                 +-------------------------------------------+
 meas.bin ------>|  page cache (one copy of the input file)  |
                 +--------+---------------+---------+--------+
                          | mmap, read-only         |
                    +-----v-----+   ...        +----v------+
                    | worker 0  |              | worker W-1|    kf_worker, started with plat_spawn
                    +--+-----^--+              +--+-----^--+
        estimates       |     | GO / STOP          |     |      two pipes per worker, 40-byte messages:
        written in      |     | READY / DONE       |     |        master -> worker : GO, STOP
        place           v     |                    v     |        worker -> master : READY, DONE, ERROR
                 +--------------------------------------------+
                 |  shared memory segment (all estimates)     |<--- master: hash, compare with serial, optionally write the file
                 +--------------------------------------------+
```

| Piece | What it is used for | Linux | Windows |
| --- | --- | --- | --- |
| **mmap** | every worker maps the same measurement file read-only; the OS keeps one copy of the pages in the page cache for all of them | `mmap` | `CreateFileMapping` + `MapViewOfFile` |
| **shared memory** | the output: one segment holding every track's estimates; each worker writes straight into its own tracks, so nothing is copied back | `shmget` + `shmat` | named `CreateFileMapping` + `MapViewOfFile` |
| **pipes** | control only: "start", "I am ready", "I am done, it took X ns, Y CPU, Z KB". Never data | `pipe2` | `CreatePipe` |
| **process creation** | one `kf_worker` per worker, with only its two pipe ends inherited | `fork` + `exec` | `CreateProcess` |

The three IPC/memory mechanisms are used for what each is best at: mmap for a large read-only input, shared memory for a large output many processes write, pipes for tiny messages that also tell the master when a worker dies.

### How a run goes

1. **Master** maps the input header, creates the shared segment (`sk_kf_<pid>`) and, for each worker, two pipes; starts the worker with `plat_spawn` passing exactly those two ends; closes its own copies of the child ends.
2. **READY barrier.** Each worker maps the input, attaches the segment and reports READY. The time until the last READY is the *start-up* cost.
3. **Rounds.** For each of R rounds the master sends every worker `GO(first track, number of tracks)` and then collects every `DONE`. The round's wall time is the *compute time*; the report uses the median of the R rounds (the first round also pays for touching every page of the output for the first time, and is reported separately as `compute_first_ms`).
4. **STOP**, `plat_wait` for every worker, check every exit code.
5. **Master** hashes the whole segment (`hash_ms`), compares it with the hash in the serial result file (`--expect-file`), and optionally writes the result file.
6. The segment is always destroyed, also when something failed.

**Work division.** Contiguous blocks of whole tracks: with `T` tracks and `W` workers every worker gets `T / W` tracks and the first `T % W` workers get one more (`kf_split_tracks`). Tracks are independent (each has its own random stream and its own filter state), so no worker ever needs another's data and there is no locking: workers write to disjoint parts of the segment. Splitting inside a track is not possible, because step `k` of a Kalman filter needs the state from step `k - 1`; that is why at most one worker per track is allowed. The static split is optimal here because every track costs the same; the report gives the measured imbalance (slowest worker / average).

**Failure handling.**

| What goes wrong | What happens |
| --- | --- |
| A worker cannot start (bad path) | `plat_spawn` fails, the master stops, removes the segment, waits for the workers it already started |
| A worker dies before READY or during a round | its pipe end closes, the master reads end-of-file instead of a message, reports *which* worker, closes the command pipes (so the other workers see end-of-file and exit), waits for all, removes the segment, exits 1, writes no CSV row |
| The master dies | the workers see end-of-file on their command pipe and exit (a leftover segment from a crash is removed by the next run, with a warning) |
| Result differs from serial | `RESULT MISMATCH` error, exit 1, no CSV row |
| A worker hangs | not detected (there is no timeout); a hang would be a bug, and the tests would time out |

These cases are tested with `--inject-failure start|compute`, which makes the last worker exit at that point.

### Timing columns (one CSV for serial and parallel runs)

`os, program, workers, io, tracks, steps, startup_ms, load_ms, compute_ms, compute_first_ms, hash_ms, write_ms, total_ms, rounds, cpu_ms, peak_kb, master_peak_kb, est_hash, verified`

- `program` is `serial` or `master`; the serial program does the same work in one process (same output buffer, same hash pass) so the comparison is fair.
- `cpu_ms`: user + system CPU time of all filtering processes for one round. On Windows CPU time is counted in scheduler ticks (about 15.6 ms), so read it as a rough figure.
- `peak_kb`: the largest peak resident memory of one filtering process (it includes the mapped input pages and the shared output pages the process touched); `master_peak_kb` is the master's own.
- `verified`: `ref` for the serial program, `yes` for a parallel run whose hash matched, `n/a` when no reference was given. The scaling scripts always pass a reference and `tools/plot_scaling.py` refuses unverified rows.

### Definitions used in the analysis

- **Speedup** `S(W)` = serial compute time / parallel compute time, from the same sweep. **Efficiency** = `S(W) / W`.
- **Karp-Flatt metric** `e(W) = (1/S - 1/W) / (1 - 1/W)`: the fraction of the run that behaved as if serial. Flat in `W` means a fixed serial part (Amdahl's law); growing with `W` means parallel overhead (shared memory bandwidth, more processes than cores, scheduling).
- **End-to-end** speedup includes start-up and hashing, so it shows what the processes cost and what stays serial (hashing, and writing the result file).

### What to expect, and why it may not be linear

- Up to the number of *physical* cores the speedup should be close to linear; beyond it, hyper-threads share a core's execution units, so the gain per extra worker is small, and beyond the number of *logical* cores there is no gain.
- The filter works on a few dozen doubles per step, so it is compute-bound, not memory-bound; the input and output stream through memory once. Memory bandwidth should matter little.
- The first round is slower than the rest (page faults while the output pages are touched for the first time); with `--repeat 3` the median hides this and `compute_first_ms` shows it.
- On Linux the measurement runs in a WSL2 virtual machine; Windows runs natively. Differences between the two are the whole stack (scheduler, page-fault path, process start-up), not just the CPU, and say nothing about Linux in general.

## What Phase 6 preserves

1. Workers call `kf_run_track` with the same `kf_model` built from the header: no change to the maths.
2. The parallel result has the same `est_hash` as `kf_serial` on the same input (checked by `kf_parallel` for 1, 2, 3, 5 and 8 workers over 11 tracks, so the blocks have different sizes, and byte-for-byte on the output file). Speedup is only reported for runs that pass this.
3. Parallelism is across tracks only.
4. Serial and parallel runs write the same CSV columns into one table.

## Commands (Phase 6)

```
bash scripts/build.sh                      # (scripts\build.bat on Windows)
bash scripts/make_data.sh large            # data/meas_large.bin and data/est_serial_large.bin
build/bin/kf_master --in data/meas_medium.bin --workers 4 --expect-file data/est_serial_medium.bin
bash scripts/run_scaling.sh                # sweep 1..16 workers, 5 times -> results/scaling_linux.csv
python3 tools/plot_scaling.py --linux results/scaling_linux.csv --windows results/scaling_windows.csv --cores 8 --physical 4
```
