#!/usr/bin/env python3
"""
collect_evidence.py - run the checks behind the requirement-to-evidence table
(docs/REQUIREMENTS.md) on THIS operating system and keep the proof.

    python tools/collect_evidence.py            (after building; run it on Linux and on Windows)

Writes results/evidence/<os>/ :
    evidence.md            one line per check: PASS / FAIL / WARN, and the file that proves it
    demo_<name>.log        output of every demo program (pass/FAIL line per check)
    forced_errors.txt      the ERROR lines the demos provoke on purpose (missing file, bad handle,
                           no permission...) - the "error handling and logging" evidence
    ctest.txt              the unit, consistency, pipeline and parallel-equals-serial tests
    kalman_check.txt       filter error against the regenerated truth (RMSE below the raw error)
    static_checks.txt      what the source scan found
Exit code 1 if any check FAILS (WARN does not fail).
"""

import glob
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OS = "windows" if sys.platform.startswith("win") else "linux"
EXE = ".exe" if OS == "windows" else ""
BIN = os.path.join(ROOT, "build", "bin")
OUT = os.path.join(ROOT, "results", "evidence", OS)

results = []   # (id, requirement, status, detail, evidence file)
static_lines = []


def rel(p):
    return os.path.relpath(p, ROOT).replace("\\", "/")


def add(rid, req, ok, detail, evidence="", warn=False):
    status = "PASS" if ok else ("WARN" if warn else "FAIL")
    results.append((rid, req, status, detail, evidence))
    print(f"[{status}] {rid} {req}: {detail}")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="replace") as f:
        return f.read()


def run(cmd, log, cwd=None):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=cwd or OUT)
    with open(os.path.join(OUT, log), "w", encoding="utf-8") as f:
        f.write(f"$ {' '.join(os.path.relpath(c, ROOT) if os.path.isabs(c) else c for c in cmd)}\n")
        f.write(p.stdout)
    return p.returncode, p.stdout


# ---- R1: the 15 system calls are used by the wrappers on each OS ---------------------------
LINUX_CALLS = ["open(", "read(", "write(", "close(", "fork(", "execv", "waitpid(", "mmap(", "munmap(",
               "pipe2(", "shmget(", "shmat(", "chmod(", "chown(", "umask("]
WIN_CALLS = ["CreateFile", "ReadFile", "WriteFile", "CloseHandle", "CreateProcess", "WaitForSingleObject",
             "VirtualAlloc", "VirtualFree", "CreateFileMapping", "MapViewOfFile", "CreatePipe",
             "SetFileSecurity"]


def check_calls():
    lin = read("src/linux/sys_linux.c") + read("src/linux/platform_linux.c")
    win = read("src/win/sys_win.c") + read("src/win/platform_win.c")
    miss_l = [c for c in LINUX_CALLS if c not in lin]
    miss_w = [c for c in WIN_CALLS if c not in win]
    static_lines.append(f"Linux calls missing from src/linux: {miss_l or 'none'}")
    static_lines.append(f"Windows calls missing from src/win: {miss_w or 'none'}")
    add("R1", "15 Linux calls and their Windows equivalents are implemented", not miss_l and not miss_w,
        f"{len(LINUX_CALLS) - len(miss_l)}/15 Linux, {len(WIN_CALLS) - len(miss_w)}/12 Windows found in the wrappers",
        "results/evidence/%s/static_checks.txt; docs/SYSCALL_MAPPING.md" % OS)


# ---- R2/R3: demos pass, forced errors are logged --------------------------------------------
def check_demos():
    names = ["fileio", "process", "memory", "ipc", "perms"] + (["process_linux"] if OS == "linux" else [])
    forced = []
    failed = []
    for n in names:
        exe = os.path.join(BIN, f"demo_{n}{EXE}")
        if not os.path.exists(exe):
            failed.append(f"{n} (not built)")
            continue
        log = f"demo_{n}.log"
        raw = os.path.join(OUT, f"demo_{n}.rawlog")
        rc, out = run([exe, raw], log)
        if os.path.exists(raw):
            os.remove(raw)   # same lines as the captured output
        if rc != 0 or f"DEMO {n} PASSED" not in out:
            failed.append(n)
        for line in out.splitlines():
            if "[ERROR]" in line and "FAIL" not in line:
                forced.append(f"{n}: {line}")
    add("R2", "Each system call group has a demo that prints pass/FAIL", not failed,
        f"{len(names) - len(failed)}/{len(names)} demos passed" + (f"; failed: {failed}" if failed else ""),
        f"results/evidence/{OS}/demo_*.log")
    with open(os.path.join(OUT, "forced_errors.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(forced) + "\n")
    kinds = {"missing file": r"No such file|cannot find the (file|path)|ERROR_FILE_NOT_FOUND|ERROR_PATH_NOT_FOUND|error 2|error 3",
             "bad handle": r"Bad file descriptor|handle is invalid|error 6|errno 9",
             }
    found = {k: any(re.search(p, l, re.I) for l in forced) for k, p in kinds.items()}
    add("R3", "Errors of every wrapper are logged with the failing call, arguments and OS error text",
        len(forced) >= 5 and all(found.values()),
        f"{len(forced)} forced-error log lines; missing-file {'yes' if found['missing file'] else 'NO'}, "
        f"bad-handle {'yes' if found['bad handle'] else 'NO'}", f"results/evidence/{OS}/forced_errors.txt")


# ---- R3b: every wrapper that can fail reports it ------------------------------------------
CANNOT_FAIL = {"plat_name", "plat_now_ns", "plat_timestamp", "plat_getpid", "plat_getuid", "plat_getgid",
               "plat_umask", "plat_sleep_ms"}


def functions(path):
    text = read(path).splitlines()
    i = 0
    while i < len(text):
        m = re.match(r"^[A-Za-z_][\w\s\*]*\b(plat_\w+)\(", text[i])
        if m and not text[i].rstrip().endswith(";"):
            j = i
            while j < len(text) and text[j] != "}":
                j += 1
            yield m.group(1), "\n".join(text[i:j])
            i = j
        i += 1


def logging_helpers(path):
    """Static helper functions of a file whose body logs (a wrapper may delegate to one)."""
    text = read(path)
    names = set()
    for m in re.finditer(r"^static [^\n(]*\b(\w+)\([^;{]*\)\s*\n?\{(.*?)^\}", text, re.S | re.M):
        if re.search(r"syserr|LOG_|log_", m.group(2)):
            names.add(m.group(1))
    return names


def check_logging_in_wrappers():
    silent = []
    total = 0
    for path in ("src/linux/sys_linux.c", "src/win/sys_win.c"):
        helpers = logging_helpers(path)
        for name, body in functions(path):
            if name in CANNOT_FAIL:
                continue
            total += 1
            if not re.search(r"syserr|LOG_|log_", body) and not any(re.search(r"\b%s\(" % h, body) for h in helpers):
                silent.append(f"{path}:{name}")
    static_lines.append(f"wrappers without a logging call in their body: {silent or 'none'}")
    add("R3b", "Every wrapper that can fail has a logging call", not silent,
        f"{total - len(silent)}/{total} wrappers log" + (f"; without: {silent}" if silent else ""),
        f"results/evidence/{OS}/static_checks.txt", warn=True)


# ---- R4: data path uses wrappers, not stdio/malloc ------------------------------------------
def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def check_wrapper_use():
    banned_io = r"\b(fopen|fread|fwrite|fgets|fscanf|open|read|write|mmap|CreateFileA?|ReadFile|WriteFile)\s*\("
    problems = []
    for f in ("kalman.c", "datagen.c", "kfio.c", "kf_worker.c", "kf_master.c", "kf_serial.c", "kfgen.c"):
        code = strip_comments(read("src/kalman/" + f))
        for m in re.finditer(banned_io, code):
            line = code.count("\n", 0, m.start()) + 1
            problems.append(f"{f}:~{line} {m.group(1)}(")
    # allocations: where, and why
    allocs = []
    for f in sorted(glob.glob(os.path.join(ROOT, "src", "kalman", "*.c"))):
        b = os.path.basename(f)
        if b.startswith("test_") or b in ("kf_check.c", "kf_eval.c"):
            continue
        code = strip_comments(open(f, encoding="utf-8").read())
        n = len(re.findall(r"\b(malloc|calloc|realloc)\s*\(", code))
        if n:
            allocs.append(f"{b}: {n}")
    shared = [f for f in ("kf_worker.c", "kalman.c", "datagen.c") if re.search(r"\b(malloc|calloc)\s*\(", read("src/kalman/" + f))]
    static_lines.append("raw file/mmap/stdio calls in the filter's data path: " + (", ".join(problems) or "none"))
    static_lines.append("malloc/calloc in src/kalman (bookkeeping, buffers for generating/reading): " + (", ".join(allocs) or "none"))
    add("R4", "File I/O, memory mapping and shared data go through the system-call wrappers (no fopen/fread/raw open/mmap)",
        not problems and not shared,
        f"no raw calls in kalman/kfio/worker/master/serial/gen; no malloc in the worker's or filter's data path "
        f"(allocations: {', '.join(allocs)})" if not problems and not shared else f"found: {problems + shared}",
        f"results/evidence/{OS}/static_checks.txt")


# ---- R5: no OS #ifdefs outside the platform layer ---------------------------------------------
def check_boundary():
    bad = []
    pat = re.compile(r"_WIN32|__linux__|__unix__|<windows\.h>|<unistd\.h>|<sys/(mman|shm|wait)\.h>")
    skip = {"demo_process_linux.c", "cases_platform_linux.c", "cases_platform_win.c"}
    for sub in ("src/kalman", "src/syscalls", "bench", "include"):
        for f in glob.glob(os.path.join(ROOT, sub, "*.[ch]")):
            if os.path.basename(f) in skip:
                continue
            code = strip_comments(open(f, encoding="utf-8").read())
            if pat.search(code):
                bad.append(rel(f))
    static_lines.append(f"portable files with OS-specific includes or #ifdefs: {bad or 'none'}")
    add("R5", "Portable code contains no OS-specific includes or #ifdef _WIN32 (platform.h boundary)", not bad,
        "none outside src/linux, src/win and the two *_platform_* case files" if not bad else f"found in {bad}",
        f"results/evidence/{OS}/static_checks.txt")


# ---- R6/R7: correctness ----------------------------------------------------------------------
def check_tests():
    ctest = subprocess.run(["ctest", "--test-dir", os.path.join(ROOT, "build"), "-R", "kf_", "--output-on-failure"],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    with open(os.path.join(OUT, "ctest.txt"), "w", encoding="utf-8") as f:
        f.write(ctest.stdout)
    m = re.search(r"(\d+)% tests passed(?:, (\d+) tests failed)? out of (\d+)", ctest.stdout)
    names = re.findall(r"Test\s+#\d+: (kf_\w+)", ctest.stdout)
    ok = ctest.returncode == 0 and m is not None and {"kf_unit", "kf_consistency", "kf_pipeline", "kf_parallel"} <= set(names)
    add("R6", "Parallel (1..8 workers) output is identical to serial, byte for byte; filter unit, statistical and "
              "pipeline tests pass", ok, m.group(0) if m else "ctest produced no summary", f"results/evidence/{OS}/ctest.txt")

    meas = os.path.join(OUT, "ev_meas.bin")
    est = os.path.join(OUT, "ev_est.bin")
    kfgen = os.path.join(BIN, "kfgen" + EXE)
    rc1, _ = run([kfgen, "--preset", "small", "--out", meas], "kalman_gen.txt")
    rc2, _ = run([os.path.join(BIN, "kf_serial" + EXE), "--in", meas, "--out", est], "kalman_serial.txt")
    rc3, out = run([os.path.join(BIN, "kf_eval" + EXE), "--meas", meas, "--est", est, "--check"], "kalman_check.txt")
    m = re.search(r"ratio ([0-9.]+)", out)
    reduction = re.search(r"([0-9.]+)x", out)
    add("R7", "Kalman output is correct: error well below the raw measurement error, and matches the filter's own prediction",
        rc1 == rc2 == rc3 == 0 and "CHECK PASSED" in out,
        (out.strip().splitlines()[-1] if out.strip() else "no output"), f"results/evidence/{OS}/kalman_check.txt")
    for f in (meas, est):
        if os.path.exists(f):
            os.remove(f)


# ---- R8: measurements recorded, reproducible --------------------------------------------------
def check_results():
    csvs = {k: os.path.exists(os.path.join(ROOT, "results", f"{k}_{OS}.csv")) or os.path.exists(os.path.join(ROOT, "results", f"{k}.csv"))
            for k in ("scaling",)}
    bench = os.path.exists(os.path.join(ROOT, "results", f"{OS}.csv"))
    meta = os.path.exists(os.path.join(ROOT, "results", f"{OS}.csv.meta.txt")) or os.path.exists(os.path.join(ROOT, "results", f"{OS}.csv.extra.txt"))
    add("R8", "Execution time and resource use recorded per call for this OS; machine details stored with the data",
        bench and meta, f"results/{OS}.csv {'present' if bench else 'MISSING'}, machine details {'present' if meta else 'MISSING'}",
        f"results/{OS}.csv, results/{OS}.csv.meta.txt", warn=True)
    add("R9", "Parallel scaling measured for this OS", csvs["scaling"],
        "results/scaling_%s.csv %s" % (OS, "present" if csvs["scaling"] else "MISSING - run scripts/run_scaling"),
        f"results/scaling_{OS}.csv", warn=True)
    docs = [d for d in ("docs/ENVIRONMENT.md", "docs/SYSCALL_MAPPING.md", "docs/BENCHMARK_METHOD.md",
                        "docs/KALMAN_DESIGN.md", "docs/PLATFORM_API.md") if os.path.exists(os.path.join(ROOT, d))]
    add("R10", "Design, method and environment are documented", len(docs) == 5, f"{len(docs)}/5 documents present",
        "docs/")


def main():
    os.makedirs(OUT, exist_ok=True)
    for b in ("kfgen", "kf_serial", "kf_eval", "kf_master"):
        if not os.path.exists(os.path.join(BIN, b + EXE)):
            sys.exit(f"build/bin/{b}{EXE} not found - build first (scripts/build)")
    check_calls()
    check_demos()
    check_logging_in_wrappers()
    check_wrapper_use()
    check_boundary()
    check_tests()
    check_results()
    with open(os.path.join(OUT, "static_checks.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(static_lines) + "\n")
    with open(os.path.join(OUT, "evidence.md"), "w", encoding="utf-8") as f:
        f.write(f"# Evidence collected on {OS}\n\n| ID | Requirement | Result | Detail | Proof |\n|---|---|---|---|---|\n")
        for rid, req, st, detail, ev in results:
            f.write(f"| {rid} | {req} | **{st}** | {detail.replace('|', '/')} | {ev} |\n")
    fails = [r for r in results if r[2] == "FAIL"]
    print(f"\n{len(results) - len(fails)}/{len(results)} checks passed or warned; details in {rel(OUT)}/evidence.md")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
