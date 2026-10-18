# Kalman filter: model, data, file format, validation

Phase 5 delivers the data generator and the serial filter. Phase 6 runs the same filter in several processes; this page fixes everything that must stay identical between the two.

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
| `kf_serial.c` | serial reference filter | via `kfio` |
| `kf_eval.c` | error of a result file against the regenerated truth | via `kfio` |
| `test_kalman.c`, `kf_check.c`, `pipeline_test.cmake` | tests | |

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

`kfgen` writes measurement files, `kf_serial` (and later the parallel master) writes estimate files; both use one format (`src/kalman/kfio.h`):

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

`ctest --test-dir build` gains three tests: `kf_unit`, `kf_consistency` and `kf_pipeline` (the last skips the Python reference if numpy is not installed).

For timing runs of `kf_serial`, leave out `--verify-input`: its hash pass over the whole file would be counted as load time.

## What Phase 6 must preserve

1. Workers call `kf_run_track` with the same `kf_model` built from the header: no change to the maths.
2. The parallel output file must have the same `est_hash` as `kf_serial` on the same input. That is the correctness test of the parallel version; speedup is only reported for runs that pass it.
3. Parallelism is across tracks only. A worker gets a contiguous block of tracks, reads its measurements from the mapped input, and writes its estimates to its block of the shared output.
4. Timing is reported as in `kf_serial`: load, compute, write, total, with the same CSV columns, so serial and parallel rows go in one table.
