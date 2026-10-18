# Test environment

Record of the machine and tools used for every benchmark in this project. Fill in the blanks once, and update it if anything changes between runs. The report quotes this file.

## Operating systems

| | Windows | Linux |
| --- | --- | --- |
| OS | Windows 10, build 19045.7725 | Ubuntu 26.04 LTS (`resolute`) |
| How it runs | Native | **WSL2** (lightweight virtual machine) |
| Shell used | `cmd` | `bash` in WSL |
| `os:` line from `hello` | `os: Windows build 19045.7725 x64` | `os: Linux 6.18.40.1-microsoft-standard-WSL2 x86_64 (WSL detected)` |

**WSL2 note for the report:** Linux numbers come from a WSL2 virtual machine, not bare-metal Linux. System call and process-creation timings can differ from native Linux, so the Windows-versus-Linux comparison is "native Windows versus Linux under WSL2", on the same physical machine.

## Toolchain

| Tool | Windows | Linux (WSL) |
| --- | --- | --- |
| C compiler | GCC 14.2.0 (MSYS2 MinGW-w64) | GCC 15.2.0 |
| C++ compiler | G++ 14.2.0 (MSYS2 MinGW-w64) | G++ 15.2.0 |
| CMake | 4.4.4 | 4.2.3 |
| Python | 3.14.3 | 3.14.4 |
| Matplotlib | 3.11.1 | 3.10.7 |
| Pandas | 3.0.1 | 2.3.3 |
| Editor | VS Code 1.138.0 | VS Code 1.138.0 (WSL server) |

Build flags: C11, `Release`, same optimisation level on both systems (set by `CMakeLists.txt`).

## Hardware

| Item | Value |
| --- | --- |
| CPU model | Intel(R) Core(TM) i7-7700HQ CPU @ 2.80GHz (up to 2.81 GHz) |
| Physical cores / logical CPUs | 4 physical cores / 8 logical CPUs |
| RAM | 16.0 GB (15.9 GB usable) |
| Disk type (SSD / HDD / NVMe) | 238 GB SATA SSD (SK Hynix HFS256G39TND-N210A) |
| WSL2 memory and CPU limits (`.wslconfig`) | default (allocates up to 50% host RAM ~8 GB, 8 logical CPUs) |

Tip: in `cmd`, `wmic cpu get name,numberofcores,numberoflogicalprocessors` and `wmic memorychip get capacity`; in WSL, `lscpu` and `free -h`.

## Conditions to keep constant during benchmark runs

- [ ] Power plan set to High performance, laptop plugged in
- [ ] Other applications closed
- [ ] Windows real-time antivirus scanning: on / off (state which; it slows `CreateFile`)
- [ ] Same machine for both operating systems
- [ ] Number of repeats per run: 5
