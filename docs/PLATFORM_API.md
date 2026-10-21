# Platform API: the wrapped system calls

Everything the portable code needs from the OS is declared in `include/platform.h`. This page lists each wrapper, the Linux call it stands for, and the Windows call planned for Phase 3. Use it as the source for the "System calls used" section of the report.

## Conventions

- Integer functions return `0` on success and `-1` on failure; pointer functions return `NULL`; handles use `PLAT_INVALID_HANDLE`.
- On failure the wrapper logs one `ERROR` line with the call, its arguments and the OS error text, then returns with the OS error code preserved. Success is silent, so benchmarks time the call and not the logging.
- The `file:line` in those log lines points at the logging helper inside `sys_linux.c`; the call and arguments in the message say which wrapper failed.
- Handles are not inherited by child processes unless listed in the `inherit` argument of `plat_spawn()`.

## The 15 calls

| Group | Wrapper | Linux | Windows |
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
| `plat_getuid`, `plat_getgid` | `getuid`, `getgid` | return the pseudo id 1000 | permission demo |
| `plat_usage_self` | `getrusage(RUSAGE_SELF)` | `GetProcessTimes` + `GetProcessMemoryInfo` | CPU time and peak memory for the benchmark (Phase 4) |
| `plat_exe_path` | `readlink("/proc/self/exe")` | `GetModuleFileName` | the benchmark driver starts a copy of itself per case |
| `plat_sleep_ms` | `nanosleep` | `Sleep` | let child processes finish before timing `plat_wait` |

## Design notes worth quoting in the report

- **`plat_spawn` reports a missing program as a failure of the call itself.** On Linux the child could only fail after `fork()`, so a close-on-exec status pipe carries the `exec` error back to the parent. Windows reports it directly from `CreateProcess`. Callers see the same behaviour on both.
- **Explicit handle inheritance.** Descriptors are opened close-on-exec and only the ones passed to `plat_spawn` are shared. This avoids a classic pipe bug where a child holds an extra copy of a write end and the reader never sees end-of-data.
- **Stale shared memory.** System V segments outlive a crashed process. `plat_shm_create` removes a leftover segment with the same name, logs a warning, and retries once.
- **Anonymous and file mappings have separate release calls** because Windows uses `VirtualFree` for one and `UnmapViewOfFile` for the other.

## Windows translation notes (`src/win/sys_win.c`)

Where Windows differs from Linux, the wrapper hides the difference. These are the behaviours to explain and compare in the report.

| Topic | Linux | Windows | What the wrapper does |
| --- | --- | --- | --- |
| Starting a program | `fork()` then `exec()` | one call, `CreateProcess` | `plat_spawn` builds a quoted command line and passes an explicit list of inherited handles (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), plus the parent's stdin, stdout and stderr, so output goes where the parent's goes |
| Handle inheritance | close-on-exec flag cleared for listed descriptors | `HANDLE_FLAG_INHERIT` set for listed handles, restored afterwards | same rule on both: only listed handles reach the child |
| End of a pipe | `read` returns 0 | `ReadFile` fails with `ERROR_BROKEN_PIPE` | `plat_read` turns the broken-pipe error into a return of 0 |
| Write to a pipe nobody reads | `SIGPIPE` kills the process (or `EPIPE` if ignored) | `WriteFile` fails | Linux: `plat_pipe` ignores `SIGPIPE` once, so `plat_write` returns -1 on both systems; the parallel master relies on this to notice a dead worker |
| Opening an invalid handle for mapping | `mmap` fails with `EBADF` | `CreateFileMapping(INVALID_HANDLE_VALUE)` succeeds, backed by the paging file | `plat_map_file` rejects the invalid handle itself |
| Deleting an open file | allowed | needs `FILE_SHARE_DELETE` | `plat_open` shares read, write and delete; `plat_unlink` retries for up to 200 ms if a virus scanner holds the file |
| Shared memory lifetime | segment survives a crash until removed | object disappears with its last handle | no stale-segment cleanup needed; creating a name that is in use fails |
| Mode bits | `chmod` sets rwx for owner, group, other | access control list (ACL) | `plat_chmod` writes a three-entry DACL (owner, the local `BUILTIN\Users` group as the "group" class, Everyone); `plat_stat_path` reads the bits back from those entries. The file's own primary group is deliberately not used, because on many accounts it is the same SID as the owner and the two classes could not be told apart. The owner entry always keeps delete, change-permissions and take-ownership rights |
| `umask` | per-process mask applied at creation | none | emulated in `plat_open`: once `plat_umask` has been called, new files get `mode & ~umask` |
| Users | numeric uid and gid | SIDs | pseudo ids: 1000 is the current user, 0 is the Administrators group, others are rejected |
| `chown` | needs root to give a file away | needs the take-ownership privilege | assigning Administrators fails in a normal shell (error 1307) and succeeds in an elevated one |
| Exit code | 0 to 255, or 128+signal | 32-bit value | passed through as an `int` |

Windows system calls are reached through the Win32 API, which sits on top of the kernel's own system calls; the benchmark therefore measures the Win32 call, not the raw kernel entry.

## Benchmark harness (`bench/`)

Built on these wrappers; see `BENCHMARK_METHOD.md` for the protocol, the case list and the CSV columns.

## Demo programs (`src/syscalls/`)

Each exits 0 only if all of its checks pass, and writes a log named `demo_<name>.log`.

| Demo | Covers | Linux | Windows |
| --- | --- | --- | --- |
| `demo_fileio` | open, read, write, close | passes | ported |
| `demo_process` | spawn, wait | passes | ported |
| `demo_process_linux` | fork, exec, wait separately (copy-on-write, `ECHILD`) | passes | not applicable |
| `demo_memory` | anonymous and file mapping | passes | ported |
| `demo_ipc` | pipe, shared memory | passes | ported |
| `demo_perms` | umask, chmod, chown | passes | ported; the ACL translation needs checking on real Windows |

On a failed check, `demo_perms` leaves `sk_perms.tmp` behind. On Windows run `icacls sk_perms.tmp` to see its access list.

`child_helper` is the program the process and IPC demos start. Run everything with `ctest --test-dir build --output-on-failure`.

Lines marked `ERROR` in the logs during a demo are forced failures (missing file, bad handle, no privilege) that the demo checks for on purpose; each is announced by an `INFO` line saying so.
