# Platform API: the wrapped system calls

Everything the portable code needs from the OS is declared in `include/platform.h`. This page lists each wrapper, the Linux call it stands for, and the Windows call planned for Phase 3. Use it as the source for the "System calls used" section of the report.

## Conventions

- Integer functions return `0` on success and `-1` on failure; pointer functions return `NULL`; handles use `PLAT_INVALID_HANDLE`.
- On failure the wrapper logs one `ERROR` line with the call, its arguments and the OS error text, then returns with the OS error code preserved. Success is silent, so benchmarks time the call and not the logging.
- The `file:line` in those log lines points at the logging helper inside `sys_linux.c`; the call and arguments in the message say which wrapper failed.
- Handles are not inherited by child processes unless listed in the `inherit` argument of `plat_spawn()`.

## The 15 calls

| Group | Wrapper | Linux | Windows (Phase 3) |
| --- | --- | --- | --- |
| File I/O | `plat_open` | `open` | `CreateFile` |
| | `plat_read` | `read` | `ReadFile` |
| | `plat_write` | `write` | `WriteFile` |
| | `plat_close` | `close` | `CloseHandle` |
| Process | `plat_spawn` | `fork` + `exec` | `CreateProcess` |
| | `plat_wait` | `wait` (`waitpid`) | `WaitForSingleObject` |
| Memory | `plat_map_anon`, `plat_unmap_anon` | `mmap`, `munmap` (anonymous) | `VirtualAlloc`, `VirtualFree` |
| | `plat_map_file`, `plat_unmap_file` | `mmap`, `munmap` (file) | `CreateFileMapping` + `MapViewOfFile`, `UnmapViewOfFile` |
| IPC | `plat_pipe` | `pipe` | `CreatePipe` |
| | `plat_shm_create` / `plat_shm_attach` | `shmget` + `shmat` | `CreateFileMapping` (named) / `OpenFileMapping` + `MapViewOfFile` |
| Permissions | `plat_chmod` | `chmod` | `SetFileSecurity` (DACL) |
| | `plat_chown` | `chown` | `SetFileSecurity` (owner) |
| | `plat_umask` | `umask` | none; CRT `_umask` as a labelled fallback |

## Helpers outside the 15

Needed around the calls and logged the same way:

| Wrapper | Linux | Windows | Why |
| --- | --- | --- | --- |
| `plat_unlink` | `unlink` | `DeleteFile` | clean up temporary files |
| `plat_stat_path` | `stat` | `GetFileAttributesEx` + security query | read size, mode, owner |
| `plat_shm_detach` | `shmdt` | `UnmapViewOfFile` | leave a shared segment |
| `plat_shm_destroy` | `shmdt` + `shmctl(IPC_RMID)` | `UnmapViewOfFile` + `CloseHandle` | remove a segment |
| `plat_getuid`, `plat_getgid` | `getuid`, `getgid` | return -1 | permission demo |

## Design notes worth quoting in the report

- **`plat_spawn` reports a missing program as a failure of the call itself.** On Linux the child could only fail after `fork()`, so a close-on-exec status pipe carries the `exec` error back to the parent. Windows reports it directly from `CreateProcess`. Callers see the same behaviour on both.
- **Explicit handle inheritance.** Descriptors are opened close-on-exec and only the ones passed to `plat_spawn` are shared. This avoids a classic pipe bug where a child holds an extra copy of a write end and the reader never sees end-of-data.
- **Stale shared memory.** System V segments outlive a crashed process. `plat_shm_create` removes a leftover segment with the same name, logs a warning, and retries once.
- **Anonymous and file mappings have separate release calls** because Windows uses `VirtualFree` for one and `UnmapViewOfFile` for the other.

## Demo programs (`src/syscalls/`)

Each exits 0 only if all of its checks pass, and writes a log named `demo_<name>.log`.

| Demo | Covers | Linux | Windows |
| --- | --- | --- | --- |
| `demo_fileio` | open, read, write, close | passes | Phase 3 |
| `demo_process` | spawn, wait | passes | Phase 3 |
| `demo_process_linux` | fork, exec, wait separately (copy-on-write, `ECHILD`) | passes | not applicable |
| `demo_memory` | anonymous and file mapping | passes | Phase 3 |
| `demo_ipc` | pipe, shared memory | passes | Phase 3 |
| `demo_perms` | umask, chmod, chown | passes | Phase 3 |

`child_helper` is the program the process and IPC demos start. Run everything with `ctest --test-dir build --output-on-failure`.

Lines marked `ERROR` in the logs during a demo are forced failures (missing file, bad handle, no privilege) that the demo checks for on purpose; each is announced by an `INFO` line saying so.
