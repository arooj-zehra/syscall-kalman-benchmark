# System call mapping: Linux to Windows

The assignment table lists 15 Linux calls but only 12 Windows functions, so several pairings are many-to-one. This is the pairing used in the project. Windows has no raw system call interface for applications; the Win32 functions below are the supported API over the kernel's system calls.

| Linux | Windows | Note |
| --- | --- | --- |
| `open`, `read`, `write`, `close` | `CreateFile`, `ReadFile`, `WriteFile`, `CloseHandle` | Handles instead of file descriptors; `CreateFile` both opens and creates |
| `fork` + `exec` | `CreateProcess` | Windows has no `fork`; `CreateProcess` does both steps in one call |
| `wait` | `WaitForSingleObject` | Waits on a process handle; exit code from `GetExitCodeProcess` |
| `mmap` (anonymous), `munmap` | `VirtualAlloc`, `VirtualFree` | `VirtualAlloc` gives anonymous memory only |
| `mmap` (file-backed) | `CreateFileMapping` + `MapViewOfFile` | Needed for the Kalman input file; an extension beyond the assignment's Memory row |
| `pipe` | `CreatePipe` | Two handles; the child inherits them through `STARTUPINFO` |
| `shmget`, `shmat` | `CreateFileMapping` (named, backed by the paging file) + `MapViewOfFile` | The same two Win32 calls serve shared memory and file mapping |
| `chmod`, `chown`, `umask` | `SetFileSecurity` | Permissions are ACLs, not mode bits. `chmod` builds a DACL, `chown` sets the owner (needs a privilege), `umask` has no Win32 equivalent; the CRT `_umask` is used only as a labelled fallback |

## Cleanup calls outside the 15

These are needed to avoid leaks and are logged like the others: `shmdt`, `shmctl(IPC_RMID)` on Linux; `UnmapViewOfFile`, `CloseHandle` on Windows.

## Status

| Group | Linux demo | Windows demo |
| --- | --- | --- |
| File I/O | | |
| Process | | |
| Memory | | |
| IPC (pipe) | | |
| IPC (shared memory) | | |
| Permissions | | |
