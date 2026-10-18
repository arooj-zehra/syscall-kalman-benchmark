# Benchmark summary

Machines:

- `linux.csv`: started:   2026-10-18 12:24:50.974; platform:  linux; os:        Linux 6.18.40.1-microsoft-standard-WSL2 x86_64 (WSL detected); cpus:      8 logical; build:     Release; compiler:  15.2.0; tmpdir:    results/tmp/; quick:     no; cpu_model: Intel(R) Core(TM) i7-7700HQ CPU @ 2.80GHz; kernel:    6.18.40.1-microsoft-standard-WSL2; memory:    7884 MB; governor:  n/a
- `windows.csv`: started:   2026-10-18 12:14:44.696; platform:  windows; os:        Windows build 19045.7725 x64; cpus:      8 logical; build:     Release; compiler:  14.2.0; tmpdir:    results\tmp/; quick:     no

Times are the median of the per-run medians; spread is (max - min) / median of the per-run medians. CPU is the user+system time of the benchmark process inside the timed calls, divided by the number of calls (a child's own CPU is not included); on Windows it is shown only when the case used at least 150 ms of CPU in total, because Windows counts CPU in ~15.6 ms ticks.

## Baseline: cost of the measurement itself

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `empty_call` | - | 2.4 ns | 2.9 ns | 0% | 2.5 ns | 2.0 ns | 2.1 ns | 0% | n/a | 0.83x |
| `plat_now_ns` | - | 30.7 ns | 31.2 ns | 1% | 30.7 ns | 52.2 ns | 52.3 ns | 0% | 53.1 ns | 1.70x |
| `plat_getpid` | - | 903.4 ns | 910.6 ns | 1% | 903.6 ns | 5.2 ns | 5.5 ns | 1% | n/a | n/a † |

## File I/O

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `plat_open` | - | 2.46 µs | 2.74 µs | 1% | 2.51 µs | 27.34 µs | 29.29 µs | 5% | 28.08 µs | 11.10x |
| `plat_open_create` | - | 11.61 µs | 13.40 µs | 5% | 11.89 µs | 250.31 µs | 278.83 µs | 7% | 239.26 µs | 21.57x |
| `plat_close` | - | 1.28 µs | 1.49 µs | 5% | 1.31 µs | 14.81 µs | 15.40 µs | 2% | 14.65 µs | 11.61x |
| `plat_unlink` | - | 8.56 µs | 10.27 µs | 10% | 8.69 µs | 191.00 µs | 201.71 µs | 1% | 139.16 µs | 22.32x |
| `plat_read` | 4 KB | 1.43 µs | 1.71 µs | 16% | 1.48 µs | 4.81 µs | 4.93 µs | 1% | 5.00 µs | 3.37x |
| `plat_write` | 4 KB | 3.36 µs | 3.44 µs | 2% | 3.37 µs | 10.22 µs | 12.04 µs | 5% | 10.00 µs | 3.04x |
| `plat_read` | 64 KB | 6.90 µs | 8.11 µs | 6% | 7.13 µs | 16.80 µs | 17.89 µs | 3% | n/a | 2.43x |
| `plat_write` | 64 KB | 19.26 µs | 19.98 µs | 2% | 19.35 µs | 26.88 µs | 29.38 µs | 6% | 27.34 µs | 1.40x |
| `plat_read` | 1 MB | 111.23 µs | 117.45 µs | 7% | 111.58 µs | 234.20 µs | 240.81 µs | 2% | 227.86 µs | 2.11x |
| `plat_write` | 1 MB | 267.96 µs | 288.91 µs | 2% | 275.73 µs | 293.34 µs | 305.40 µs | 1% | 292.97 µs | 1.09x |

## Process creation

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `plat_spawn` | - | 550.05 µs | 662.55 µs | 1% | 192.60 µs | 1.56 ms | 1.68 ms | 2% | n/a | 2.83x |
| `plat_wait` | - | 10.76 µs | 14.40 µs | 2% | 12.82 µs | 67.74 µs | 88.01 µs | 9% | n/a | 6.29x |
| `plat_spawn_wait` | - | 1.09 ms | 1.30 ms | 8% | 226.15 µs | 7.56 ms | 8.04 ms | 1% | n/a | 6.94x |

## Memory mapping

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `plat_map_anon` | 4 KB | 1.99 µs | 2.34 µs | 7% | 2.08 µs | 1.82 µs | 1.93 µs | 2% | n/a | 0.91x |
| `plat_map_anon_touch` | 4 KB | 4.76 µs | 5.49 µs | 5% | 4.84 µs | 3.85 µs | 4.40 µs | 2% | n/a | 0.81x |
| `plat_unmap_anon` | 4 KB | 3.13 µs | 3.74 µs | 21% | 3.25 µs | 2.62 µs | 2.73 µs | 2% | n/a | 0.83x |
| `plat_unmap_anon_touched` | 4 KB | 6.14 µs | 6.86 µs | 33% | 6.27 µs | 2.92 µs | 3.06 µs | 0% | n/a | 0.48x |
| `plat_map_file` | 4 KB | 3.26 µs | 4.49 µs | 4% | 3.36 µs | 14.12 µs | 14.45 µs | 1% | n/a | 4.33x |
| `plat_map_file_touch` | 4 KB | 5.21 µs | 6.18 µs | 3% | 5.30 µs | 16.36 µs | 16.60 µs | 1% | 16.78 µs | 3.14x |
| `plat_unmap_file` | 4 KB | 2.86 µs | 3.63 µs | 49% | 2.95 µs | 2.78 µs | 2.83 µs | 3% | n/a | 0.97x |
| `plat_unmap_file_touched` | 4 KB | 5.07 µs | 5.76 µs | 5% | 5.17 µs | 2.90 µs | 2.99 µs | 1% | n/a | 0.57x |
| `plat_map_anon` | 1 MB | 1.88 µs | 2.24 µs | 1% | 1.94 µs | 1.86 µs | 1.96 µs | 1% | n/a | 0.99x |
| `plat_map_anon_touch` | 1 MB | 610.02 µs | 631.14 µs | 3% | 611.73 µs | 159.03 µs | 160.61 µs | 8% | 155.64 µs | 0.26x |
| `plat_unmap_anon` | 1 MB | 2.66 µs | 3.31 µs | 6% | 2.79 µs | 2.50 µs | 2.83 µs | 13% | n/a | 0.94x |
| `plat_unmap_anon_touched` | 1 MB | 82.06 µs | 84.45 µs | 4% | 86.50 µs | 105.72 µs | 106.29 µs | 16% | 106.81 µs | 1.29x |
| `plat_map_file` | 1 MB | 2.99 µs | 3.96 µs | 1% | 3.11 µs | 14.15 µs | 14.32 µs | 1% | n/a | 4.74x |
| `plat_map_file_touch` | 1 MB | 39.65 µs | 41.28 µs | 3% | 39.80 µs | 476.33 µs | 479.67 µs | 2% | 479.13 µs | 12.01x |
| `plat_unmap_file` | 1 MB | 2.78 µs | 3.27 µs | 1% | 2.86 µs | 2.91 µs | 2.97 µs | 8% | n/a | 1.05x |
| `plat_unmap_file_touched` | 1 MB | 12.72 µs | 14.59 µs | 18% | 13.03 µs | 47.33 µs | 47.99 µs | 37% | 45.78 µs | 3.72x |
| `plat_map_anon` | 16 MB | 1.85 µs | 2.27 µs | 1% | 1.99 µs | 1.97 µs | 2.09 µs | 0% | n/a | 1.06x |
| `plat_map_anon_touch` | 16 MB | 9.56 ms | 9.70 ms | 1% | 9.57 ms | 2.17 ms | 2.28 ms | 2% | 2.15 ms | 0.23x |
| `plat_unmap_anon` | 16 MB | 2.67 µs | 3.25 µs | 1% | 2.79 µs | 2.51 µs | 2.98 µs | 14% | n/a | 0.94x |
| `plat_unmap_anon_touched` | 16 MB | 1.16 ms | 1.19 ms | 1% | 1.17 ms | 1.74 ms | 1.77 ms | 3% | 1.51 ms | 1.50x |
| `plat_map_file` | 16 MB | 2.85 µs | 3.75 µs | 2% | 3.02 µs | 14.57 µs | 15.17 µs | 1% | n/a | 5.11x |
| `plat_map_file_touch` | 16 MB | 614.22 µs | 631.01 µs | 1% | 615.87 µs | 8.12 ms | 8.17 ms | 3% | 8.20 ms | 13.22x |
| `plat_unmap_file` | 16 MB | 2.68 µs | 3.66 µs | 3% | 2.91 µs | 2.95 µs | 3.08 µs | 7% | n/a | 1.10x |
| `plat_unmap_file_touched` | 16 MB | 112.78 µs | 123.58 µs | 5% | 114.88 µs | 776.94 µs | 788.80 µs | 5% | 830.08 µs | 6.89x |

## Pipes and shared memory

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `plat_pipe` | - | 2.63 µs | 3.28 µs | 2% | 2.77 µs | 9.13 µs | 9.57 µs | 2% | n/a | 3.48x |
| `plat_pipe_write_read` | 64 B | 2.31 µs | 2.44 µs | 1% | 2.33 µs | 2.96 µs | 3.03 µs | 1% | n/a | 1.28x |
| `plat_pipe_write_read` | 512 B | 2.32 µs | 2.42 µs | 1% | 2.33 µs | 3.48 µs | 3.60 µs | 1% | n/a | 1.50x |
| `plat_pipe_write_read` | 2 KB | 2.37 µs | 2.47 µs | 4% | 2.39 µs | 3.80 µs | 3.88 µs | 3% | 3.91 µs | 1.60x |
| `plat_shm_create` | 4 KB | 8.57 µs | 11.12 µs | 5% | 8.55 µs | 11.65 µs | 12.66 µs | 2% | n/a | 1.36x |
| `plat_shm_destroy` | 4 KB | 7.06 µs | 9.37 µs | 3% | 7.03 µs | 5.17 µs | 5.67 µs | 11% | n/a | 0.73x |
| `plat_shm_attach` | 4 KB | 5.08 µs | 6.52 µs | 3% | 5.52 µs | 6.39 µs | 7.00 µs | 1% | n/a | 1.26x |
| `plat_shm_detach` | 4 KB | 3.21 µs | 4.23 µs | 3% | 3.46 µs | 3.63 µs | 3.95 µs | 11% | n/a | 1.13x |
| `plat_shm_create` | 1 MB | 8.36 µs | 10.65 µs | 1% | 8.29 µs | 13.86 µs | 16.80 µs | 7% | n/a | 1.66x |
| `plat_shm_destroy` | 1 MB | 7.11 µs | 9.85 µs | 2% | 7.18 µs | 11.01 µs | 11.82 µs | 4% | n/a | 1.55x |
| `plat_shm_attach` | 1 MB | 4.90 µs | 6.63 µs | 1% | 5.27 µs | 6.43 µs | 7.14 µs | 2% | n/a | 1.31x |
| `plat_shm_detach` | 1 MB | 3.27 µs | 4.78 µs | 2% | 3.58 µs | 4.13 µs | 4.40 µs | 1% | n/a | 1.26x |
| `plat_shm_create` | 16 MB | 8.65 µs | 10.82 µs | 2% | 8.39 µs | 13.96 µs | 15.60 µs | 1% | n/a | 1.61x |
| `plat_shm_destroy` | 16 MB | 7.36 µs | 9.53 µs | 2% | 7.43 µs | 7.25 µs | 7.65 µs | 1% | n/a | 0.99x |
| `plat_shm_attach` | 16 MB | 4.90 µs | 6.34 µs | 3% | 5.24 µs | 6.57 µs | 6.95 µs | 1% | n/a | 1.34x |
| `plat_shm_detach` | 16 MB | 3.40 µs | 4.78 µs | 7% | 3.77 µs | 4.36 µs | 4.59 µs | 10% | n/a | 1.28x |

## File permissions

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `plat_chmod` | - | 3.04 µs | 3.28 µs | 4% | 3.07 µs | 174.68 µs | 179.03 µs | 1% | 175.00 µs | 57.47x |
| `plat_chown` | - | 3.18 µs | 3.48 µs | 3% | 3.23 µs | 106.10 µs | 126.90 µs | 1% | 106.25 µs | 33.38x |
| `plat_stat_path` | - | 2.03 µs | 2.17 µs | 3% | 2.05 µs | 115.45 µs | 116.27 µs | 1% | 115.62 µs | 56.90x |
| `plat_umask` | - | 888.0 ns | 915.4 ns | 0% | 896.6 ns | 4.0 ns | 4.2 ns | 0% | n/a | n/a † |

## Linux-only: fork()

| call | size | Linux median | Linux p95 | Linux spread | Linux CPU/call | Windows median | Windows p95 | Windows spread | Windows CPU/call | Windows / Linux |
|---|---|---|---|---|---|---|---|---|---|---|
| `fork_wait` | - | 372.60 µs | 437.85 µs | 18% | 171.26 µs | - | - | - | - | - |
| `fork_wait_64mb` | 64 MB | 2.05 ms | 2.24 ms | 7% | 725.36 µs | - | - | - | - | - |

† On Windows this call does not enter the kernel (`plat_getpid` reads a thread field, `plat_umask` stores a number), so a ratio against the Linux system call is not meaningful.

