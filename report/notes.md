# Report notes

Spend about 20 minutes after each phase dropping the key points, numbers and surprises here. The final report in Phase 8 is assembled from these notes.

## Phase 1: setup

- Environment decisions (WSL2 for Linux, native Windows with MSYS2 MinGW-w64):
- Problems hit and how they were fixed:
- Smoke test output (paste both `os:` lines):

## Phase 2: Linux system calls

- Per call: what it does, arguments that matter, return values, error cases seen:
- Surprises:

## Phase 3: Windows equivalents

- Per call pairing and why (see `docs/SYSCALL_MAPPING.md`):
- Behaviour differences found (handles vs descriptors, ACLs vs mode bits):

## Phase 4: benchmark harness

- Method (loop counts, warm-up, repeats) and why:
- Timer resolution and call overhead from `hello`:

## Phase 5: serial Kalman filter

- Model, matrices, noise settings:
- Workload size and serial runtime:
- RMSE of filtered output versus raw measurements:

## Phase 6: Kalman integration

- Design decisions (parallelism across tracks, worker contract, failure handling):
- Speedup table notes:
- Bugs that taught something:

## Phase 7: comparison and validation

- Each large Linux/Windows gap: hypothesis, test, result:
- Requirement-to-evidence table status:

## Lessons learned

-
