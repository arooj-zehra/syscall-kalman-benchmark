/*
 * sys_win.c - Windows (Win32) implementation of the wrapped system calls in platform.h.
 *
 * Same contract as src/linux/sys_linux.c: a failure is logged with the call, its
 * arguments and the Windows error text, then the wrapper returns -1 / NULL with
 * GetLastError() preserved. Success is silent and does not allocate.
 *
 * Where Windows differs from Linux, the wrapper hides the difference and the
 * comment says how:
 *   - fork+exec        -> CreateProcess, with an explicit list of inherited handles
 *   - mode bits        -> a three-entry DACL (owner, BUILTIN\Users as "group", Everyone)
 *   - umask            -> emulated inside plat_open (Windows has no umask)
 *   - uid / gid        -> pseudo ids: 0 = Administrators, 1000 = the current user
 *   - read at EOF      -> broken pipe is reported as end-of-data, like Linux
 */

#include "platform.h"

#include "log.h"

#include <windows.h>

#include <sddl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSEUDO_UID 1000
#define PSEUDO_GID 1000
#define IO_CHUNK_MAX 0x40000000u /* ReadFile/WriteFile take a DWORD; stay well below 4 GiB */
#define CMDLINE_MAX 32768        /* CreateProcess limit for the command line */
#define MAX_SHARED_HANDLES 16

/* Log "<what> failed: <Windows error text>" and leave the last-error code as the failing call set it. */
static void syserr(const char *fmt, ...) LOG_PRINTF_FMT(1, 2);

static void syserr(const char *fmt, ...)
{
    DWORD saved = GetLastError();
    char what[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(what, sizeof what, fmt, ap);
    va_end(ap);

    SetLastError(saved);
    log_syserr_at(LOG_LEVEL_ERROR, __FILE__, __LINE__, what);
    SetLastError(saved);
}

static unsigned long long ull(size_t v)
{
    return (unsigned long long)v;
}

/* ======================= Permissions (shared by open, chmod, stat) ======== */

static unsigned g_umask = 022;
static int g_umask_active = 0; /* umask emulation stays off until plat_umask() is called */

/* Rights that always go to the owner entry so the owner can still change
 * permissions, take ownership and delete the file, as on POSIX. */
#define OWNER_ADMIN_RIGHTS (DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER | SYNCHRONIZE)

static DWORD rights_from_bits(unsigned bits)
{
    DWORD r = 0;

    if (bits & 4u) {
        r |= FILE_GENERIC_READ;
    }
    if (bits & 2u) {
        r |= FILE_GENERIC_WRITE;
    }
    if (bits & 1u) {
        r |= FILE_GENERIC_EXECUTE;
    }
    return r;
}

static unsigned bits_from_rights(DWORD mask)
{
    unsigned bits = 0;

    if (mask & FILE_READ_DATA) {
        bits |= 4u;
    }
    if (mask & FILE_WRITE_DATA) {
        bits |= 2u;
    }
    if (mask & FILE_EXECUTE) {
        bits |= 1u;
    }
    return bits;
}

/* Fetch part of a file's security descriptor into a malloc'ed buffer. */
static int load_security(const char *path, SECURITY_INFORMATION info, BYTE **out)
{
    DWORD need = 0;
    BYTE *buf;

    (void)GetFileSecurityA(path, info, NULL, 0, &need);
    if (need == 0) {
        return -1; /* last error describes why (file not found, access denied, ...) */
    }
    buf = (BYTE *)malloc(need);
    if (buf == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return -1;
    }
    if (!GetFileSecurityA(path, info, (PSECURITY_DESCRIPTOR)buf, need, &need)) {
        DWORD e = GetLastError();
        free(buf);
        SetLastError(e);
        return -1;
    }
    *out = buf;
    return 0;
}

static int world_sid(BYTE *buf, DWORD cap)
{
    DWORD size = cap;
    return CreateWellKnownSid(WinWorldSid, NULL, buf, &size) ? 0 : -1;
}

/* The "group" class of the mode bits is the local BUILTIN\Users group. The file's own primary
 * group is not used: on many accounts it is the same SID as the owner, which would make the
 * owner and group classes impossible to tell apart. A fixed well-known SID never collides. */
static int users_sid(BYTE *buf, DWORD cap)
{
    DWORD size = cap;
    return CreateWellKnownSid(WinBuiltinUsersSid, NULL, buf, &size) ? 0 : -1;
}

/* Replace the file's DACL by owner / Users / Everyone entries built from mode bits. */
static int apply_mode(const char *path, unsigned mode)
{
    BYTE *sec = NULL;
    BYTE world[SECURITY_MAX_SID_SIZE];
    BYTE users[SECURITY_MAX_SID_SIZE];
    PSID owner = NULL;
    BOOL defaulted;
    unsigned ubits = (mode >> 6) & 7u;
    unsigned gbits = (mode >> 3) & 7u;
    unsigned obits = mode & 7u;
    DWORD acl_size;
    PACL acl;
    SECURITY_DESCRIPTOR sd;
    int rc = -1;

    if (load_security(path, OWNER_SECURITY_INFORMATION, &sec) != 0) {
        syserr("GetFileSecurity(%s)", path);
        return -1;
    }
    if (!GetSecurityDescriptorOwner(sec, &owner, &defaulted) || owner == NULL ||
        world_sid(world, sizeof world) != 0 || users_sid(users, sizeof users) != 0) {
        syserr("read owner of %s", path);
        free(sec);
        return -1;
    }

    acl_size = (DWORD)(sizeof(ACL) + 3 * (sizeof(ACCESS_ALLOWED_ACE) - sizeof(DWORD)) +
                       GetLengthSid(owner) + GetLengthSid(users) + GetLengthSid(world));
    acl_size = (acl_size + 3u) & ~3u;
    acl = (PACL)malloc(acl_size);
    if (acl == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        syserr("allocate ACL for %s", path);
        free(sec);
        return -1;
    }

    if (InitializeAcl(acl, acl_size, ACL_REVISION) &&
        AddAccessAllowedAce(acl, ACL_REVISION, rights_from_bits(ubits) | OWNER_ADMIN_RIGHTS, owner) &&
        (gbits == 0 || AddAccessAllowedAce(acl, ACL_REVISION, rights_from_bits(gbits), users)) &&
        (obits == 0 || AddAccessAllowedAce(acl, ACL_REVISION, rights_from_bits(obits), world)) &&
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE)) {
        /* PROTECTED stops the parent folder's entries from being merged back in. */
        if (SetFileSecurityA(path, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, &sd)) {
            rc = 0;
        } else {
            syserr("SetFileSecurity(%s, DACL for mode 0%o)", path, mode);
        }
    } else {
        syserr("build DACL for %s (mode 0%o)", path, mode);
    }

    free(acl);
    free(sec);
    return rc;
}

/* Debug aid: log a SID in the usual S-1-5-... text form. */
static void log_sid(const char *label, PSID sid)
{
    char *text = NULL;

    if (sid != NULL && ConvertSidToStringSidA(sid, &text)) {
        LOG_DEBUG("    %s %s", label, text);
        LocalFree(text);
    }
}

/* Fill mode and uid from the file's security descriptor. Set SK_LOG_LEVEL=debug to see every entry. */
static void security_to_stat(const char *path, plat_stat *st, DWORD readonly_attr)
{
    BYTE *sec = NULL;
    BYTE world[SECURITY_MAX_SID_SIZE];
    BYTE users[SECURITY_MAX_SID_SIZE];
    BYTE admins[SECURITY_MAX_SID_SIZE];
    DWORD admins_size = sizeof admins;
    PSID owner = NULL;
    PACL dacl = NULL;
    BOOL present = FALSE;
    BOOL defaulted;
    unsigned mode = 0;
    DWORD i;

    st->mode = readonly_attr ? 0444u : 0666u; /* fallback if the descriptor cannot be read */

    if (load_security(path, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &sec) != 0) {
        syserr("GetFileSecurity(%s)", path);
        return;
    }
    (void)GetSecurityDescriptorOwner(sec, &owner, &defaulted);
    LOG_DEBUG("  security of %s", path);
    log_sid("owner", owner);

    if (!GetSecurityDescriptorDacl(sec, &present, &dacl, &defaulted) || !present || dacl == NULL) {
        mode = 0777; /* no DACL at all means everyone has full access */
        LOG_DEBUG("    no DACL: full access for everyone");
    } else if (world_sid(world, sizeof world) == 0 && users_sid(users, sizeof users) == 0) {
        for (i = 0; i < dacl->AceCount; i++) {
            void *ace = NULL;
            ACCESS_ALLOWED_ACE *a;
            PSID sid;
            unsigned bits;

            if (!GetAce(dacl, i, &ace) || ace == NULL) {
                continue;
            }
            a = (ACCESS_ALLOWED_ACE *)ace;
            if (a->Header.AceType != ACCESS_ALLOWED_ACE_TYPE || (a->Header.AceFlags & INHERIT_ONLY_ACE)) {
                continue;
            }
            sid = (PSID)&a->SidStart;
            bits = bits_from_rights(a->Mask);
            LOG_DEBUG("    allow ace %lu: mask 0x%08lx -> rwx %u", (unsigned long)i, (unsigned long)a->Mask, bits);
            log_sid("  for", sid);
            if (owner != NULL && EqualSid(sid, owner)) {
                mode |= bits << 6;
            }
            if (EqualSid(sid, users)) {
                mode |= bits << 3;
            }
            if (EqualSid(sid, world)) {
                mode |= bits;
            }
        }
    }
    LOG_DEBUG("    resulting mode 0%o", mode);
    st->mode = mode;

    if (owner != NULL && CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admins, &admins_size) &&
        EqualSid(owner, admins)) {
        st->uid = 0;
    } else if (owner != NULL) {
        /* Compare with the current user. */
        union {
            TOKEN_USER tu;
            BYTE raw[512];
        } u;
        HANDLE tok;
        DWORD len = 0;

        st->uid = -1;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            if (GetTokenInformation(tok, TokenUser, &u, sizeof u, &len) && EqualSid(owner, u.tu.User.Sid)) {
                st->uid = PSEUDO_UID;
            }
            CloseHandle(tok);
        }
    }
    free(sec);
}

/* ======================= File I/O: CreateFile ReadFile WriteFile CloseHandle = */

plat_handle plat_open(const char *path, int flags, unsigned mode)
{
    DWORD access = 0;
    DWORD disposition;
    DWORD last;
    HANDLE h;

    if (flags & PLAT_OPEN_READ) {
        access |= GENERIC_READ;
    }
    if (flags & PLAT_OPEN_WRITE) {
        access |= GENERIC_WRITE;
    }
    if (access == 0) {
        access = GENERIC_READ;
    }

    if ((flags & PLAT_OPEN_CREATE) && (flags & PLAT_OPEN_TRUNC)) {
        disposition = CREATE_ALWAYS;
    } else if (flags & PLAT_OPEN_CREATE) {
        disposition = OPEN_ALWAYS;
    } else if (flags & PLAT_OPEN_TRUNC) {
        disposition = TRUNCATE_EXISTING;
    } else {
        disposition = OPEN_EXISTING;
    }

    /* Not inheritable (no SECURITY_ATTRIBUTES). Sharing all three ways matches POSIX, where
     * a file can be read, written and unlinked while another process has it open. */
    h = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disposition,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    last = GetLastError();
    if (h == INVALID_HANDLE_VALUE) {
        syserr("CreateFile(%s, flags=0x%x)", path, (unsigned)flags);
        return PLAT_INVALID_HANDLE;
    }

    /* umask emulation: a file created now gets (mode & ~umask). An existing file keeps its permissions. */
    if ((flags & PLAT_OPEN_CREATE) && g_umask_active && last != ERROR_ALREADY_EXISTS) {
        if (apply_mode(path, mode & ~g_umask & 0777u) != 0) {
            LOG_WARN("could not apply mode 0%o to new file %s", mode & ~g_umask & 0777u, path);
        }
    }
    return (plat_handle)h;
}

int64_t plat_read(plat_handle h, void *buf, size_t len)
{
    DWORD got = 0;
    DWORD chunk = len > IO_CHUNK_MAX ? IO_CHUNK_MAX : (DWORD)len;

    if (!ReadFile((HANDLE)h, buf, chunk, &got, NULL)) {
        DWORD e = GetLastError();
        if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) {
            return 0; /* every write end of the pipe is closed: end of data, as on Linux */
        }
        SetLastError(e);
        syserr("ReadFile(handle=%lld, len=%llu)", (long long)h, ull(len));
        return -1;
    }
    return (int64_t)got;
}

int64_t plat_write(plat_handle h, const void *buf, size_t len)
{
    DWORD put = 0;
    DWORD chunk = len > IO_CHUNK_MAX ? IO_CHUNK_MAX : (DWORD)len;

    if (!WriteFile((HANDLE)h, buf, chunk, &put, NULL)) {
        syserr("WriteFile(handle=%lld, len=%llu)", (long long)h, ull(len));
        return -1;
    }
    return (int64_t)put;
}

int plat_close(plat_handle h)
{
    if (!CloseHandle((HANDLE)h)) {
        syserr("CloseHandle(handle=%lld)", (long long)h);
        return -1;
    }
    return 0;
}

int plat_unlink(const char *path)
{
    DWORD e = 0;
    int attempt;

    /* Virus scanners can hold a freshly written file for a few milliseconds. Retry briefly on a
     * sharing violation or access denied before reporting failure. */
    for (attempt = 0; attempt < 10; attempt++) {
        if (DeleteFileA(path)) {
            return 0;
        }
        e = GetLastError();
        if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED) {
            break;
        }
        Sleep(20);
    }
    SetLastError(e);
    syserr("DeleteFile(%s)", path);
    return -1;
}

int plat_stat_path(const char *path, plat_stat *st)
{
    WIN32_FILE_ATTRIBUTE_DATA d;

    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &d)) {
        syserr("GetFileAttributesEx(%s)", path);
        return -1;
    }
    st->size = ((int64_t)d.nFileSizeHigh << 32) | (int64_t)d.nFileSizeLow;
    st->mode = 0;
    st->uid = -1;
    st->gid = -1;
    security_to_stat(path, st, d.dwFileAttributes & FILE_ATTRIBUTE_READONLY);
    return 0;
}

/* ======================= Process: CreateProcess WaitForSingleObject ======= */

static int put_char(char *out, size_t cap, size_t *len, char c)
{
    if (*len + 1 >= cap) {
        return -1;
    }
    out[(*len)++] = c;
    return 0;
}

/* Append one argument using the quoting rules the C runtime parses back into the same argv. */
static int append_arg(char *out, size_t cap, size_t *len, const char *arg)
{
    const char *p;
    int need_quote = arg[0] == '\0' || strpbrk(arg, " \t\n\v\"") != NULL;
    size_t i;

    if (!need_quote) {
        for (p = arg; *p != '\0'; p++) {
            if (put_char(out, cap, len, *p) != 0) {
                return -1;
            }
        }
        return 0;
    }

    if (put_char(out, cap, len, '"') != 0) {
        return -1;
    }
    for (p = arg;; p++) {
        size_t backslashes = 0;

        while (*p == '\\') {
            p++;
            backslashes++;
        }
        if (*p == '\0') {
            for (i = 0; i < backslashes * 2; i++) {
                if (put_char(out, cap, len, '\\') != 0) {
                    return -1;
                }
            }
            break;
        }
        if (*p == '"') {
            for (i = 0; i < backslashes * 2 + 1; i++) {
                if (put_char(out, cap, len, '\\') != 0) {
                    return -1;
                }
            }
        } else {
            for (i = 0; i < backslashes; i++) {
                if (put_char(out, cap, len, '\\') != 0) {
                    return -1;
                }
            }
        }
        if (put_char(out, cap, len, *p) != 0) {
            return -1;
        }
    }
    return put_char(out, cap, len, '"');
}

int plat_spawn(const char *exe, char *const argv[],
               const plat_handle *inherit, int n_inherit, plat_proc *out)
{
    char *cmd;
    size_t cmd_len = 0;
    HANDLE shared[MAX_SHARED_HANDLES];
    DWORD saved_flags[MAX_SHARED_HANDLES];
    int count = 0;
    int i;
    int j;
    DWORD std_ids[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    HANDLE std_h[3];
    STARTUPINFOEXA si;
    PROCESS_INFORMATION pi;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    SIZE_T attrs_size = 0;
    BOOL created;
    DWORD err = 0;
    int rc = -1;

    cmd = (char *)malloc(CMDLINE_MAX);
    if (cmd == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        syserr("CreateProcess(%s): command line buffer", exe);
        return -1;
    }
    for (i = 0; argv[i] != NULL; i++) {
        if ((i > 0 && put_char(cmd, CMDLINE_MAX, &cmd_len, ' ') != 0) ||
            append_arg(cmd, CMDLINE_MAX, &cmd_len, argv[i]) != 0) {
            SetLastError(ERROR_BUFFER_OVERFLOW);
            syserr("CreateProcess(%s): command line longer than %d characters", exe, CMDLINE_MAX - 1);
            free(cmd);
            return -1;
        }
    }
    cmd[cmd_len] = '\0';

    /* Handles the child may inherit: the ones the caller listed, plus this process's
     * stdin/stdout/stderr so the child's output goes where ours goes. Duplicates in the
     * list make CreateProcess fail, so each handle appears once. */
    for (i = 0; i < n_inherit; i++) {
        HANDLE h = (HANDLE)inherit[i];
        DWORD flags;

        if (!GetHandleInformation(h, &flags)) {
            syserr("CreateProcess(%s): handle %lld to inherit is not valid", exe, (long long)inherit[i]);
            goto cleanup_flags;
        }
        for (j = 0; j < count && shared[j] != h; j++) {
        }
        if (j == count) {
            if (count >= MAX_SHARED_HANDLES - 3) {
                SetLastError(ERROR_TOO_MANY_OPEN_FILES);
                syserr("CreateProcess(%s): too many handles to inherit", exe);
                goto cleanup_flags;
            }
            shared[count] = h;
            saved_flags[count] = flags;
            count++;
        }
    }
    for (i = 0; i < 3; i++) {
        HANDLE h = GetStdHandle(std_ids[i]);
        DWORD flags;

        std_h[i] = NULL;
        if (h == NULL || h == INVALID_HANDLE_VALUE || !GetHandleInformation(h, &flags)) {
            continue;
        }
        std_h[i] = h;
        for (j = 0; j < count && shared[j] != h; j++) {
        }
        if (j == count) {
            shared[count] = h;
            saved_flags[count] = flags;
            count++;
        }
    }

    for (i = 0; i < count; i++) {
        (void)SetHandleInformation(shared[i], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }

    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.StartupInfo.cb = sizeof si;

    if (count > 0) {
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = std_h[0];
        si.StartupInfo.hStdOutput = std_h[1];
        si.StartupInfo.hStdError = std_h[2];

        (void)InitializeProcThreadAttributeList(NULL, 1, 0, &attrs_size);
        attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(attrs_size);
        if (attrs == NULL || !InitializeProcThreadAttributeList(attrs, 1, 0, &attrs_size) ||
            !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, shared,
                                       (SIZE_T)count * sizeof(HANDLE), NULL, NULL)) {
            if (attrs == NULL) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            }
            syserr("CreateProcess(%s): handle list", exe);
            goto cleanup_attrs;
        }
        si.lpAttributeList = attrs;
    }

    created = CreateProcessA(exe, cmd, NULL, NULL, count > 0 ? TRUE : FALSE,
                             count > 0 ? EXTENDED_STARTUPINFO_PRESENT : 0, NULL, NULL, &si.StartupInfo, &pi);
    err = GetLastError();

    if (created) {
        CloseHandle(pi.hThread);
        *out = (plat_proc)pi.hProcess;
        rc = 0;
    }

cleanup_attrs:
    if (attrs != NULL) {
        DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
cleanup_flags:
    for (i = 0; i < count; i++) {
        (void)SetHandleInformation(shared[i], HANDLE_FLAG_INHERIT, saved_flags[i] & HANDLE_FLAG_INHERIT);
    }
    free(cmd);
    if (rc != 0 && err != 0) {
        SetLastError(err);
        syserr("CreateProcess(%s)", exe);
    }
    return rc;
}

int plat_wait(plat_proc p, int *exit_code)
{
    DWORD code = 0;

    if (WaitForSingleObject((HANDLE)p, INFINITE) != WAIT_OBJECT_0) {
        syserr("WaitForSingleObject(process=%lld)", (long long)p);
        return -1;
    }
    if (!GetExitCodeProcess((HANDLE)p, &code)) {
        syserr("GetExitCodeProcess(process=%lld)", (long long)p);
        return -1;
    }
    CloseHandle((HANDLE)p); /* releases the process object, like waitpid() reaping a child */
    *exit_code = (int)code;
    return 0;
}

/* ======================= Memory: VirtualAlloc, file mappings ============== */

void *plat_map_anon(size_t len)
{
    void *p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); /* zero-filled */

    if (p == NULL) {
        syserr("VirtualAlloc(len=%llu)", ull(len));
        return NULL;
    }
    return p;
}

int plat_unmap_anon(void *addr, size_t len)
{
    (void)len; /* MEM_RELEASE requires a size of 0: the whole reservation is freed */
    if (!VirtualFree(addr, 0, MEM_RELEASE)) {
        syserr("VirtualFree(%p)", addr);
        return -1;
    }
    return 0;
}

void *plat_map_file(plat_handle h, size_t len, int writable)
{
    HANDLE mapping;
    void *view;
    unsigned long long size = ull(len);

    if (h == PLAT_INVALID_HANDLE) {
        /* CreateFileMapping would treat INVALID_HANDLE_VALUE as "use the paging file" and succeed. */
        SetLastError(ERROR_INVALID_HANDLE);
        syserr("CreateFileMapping(handle=%lld, len=%llu)", (long long)h, size);
        return NULL;
    }

    mapping = CreateFileMappingA((HANDLE)h, NULL, writable ? PAGE_READWRITE : PAGE_READONLY,
                                 (DWORD)(size >> 32), (DWORD)(size & 0xffffffffu), NULL);
    if (mapping == NULL) {
        syserr("CreateFileMapping(handle=%lld, len=%llu, %s)", (long long)h, size,
               writable ? "read-write" : "read-only");
        return NULL;
    }
    view = MapViewOfFile(mapping, writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, len);
    if (view == NULL) {
        syserr("MapViewOfFile(handle=%lld, len=%llu)", (long long)h, size);
    }
    CloseHandle(mapping); /* the view keeps the mapping object alive */
    return view;
}

int plat_unmap_file(void *addr, size_t len)
{
    (void)len;
    if (!UnmapViewOfFile(addr)) {
        syserr("UnmapViewOfFile(%p)", addr);
        return -1;
    }
    return 0;
}

/* ======================= IPC: CreatePipe, named file mappings ============= */

int plat_pipe(plat_handle *rd, plat_handle *wr)
{
    HANDLE r;
    HANDLE w;

    /* NULL security attributes: neither end is inheritable until plat_spawn() lists it. */
    if (!CreatePipe(&r, &w, NULL, 0)) {
        syserr("CreatePipe()");
        return -1;
    }
    *rd = (plat_handle)r;
    *wr = (plat_handle)w;
    return 0;
}

static void shm_object_name(const char *name, char *out, size_t cap)
{
    snprintf(out, cap, "Local\\%s", name); /* visible to processes in the same logon session */
}

int plat_shm_create(const char *name, size_t len, plat_shm *shm)
{
    char full[256];
    unsigned long long size = ull(len);
    HANDLE h;
    void *addr;

    shm_object_name(name, full, sizeof full);
    SetLastError(0);
    h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, (DWORD)(size >> 32),
                           (DWORD)(size & 0xffffffffu), full);
    if (h == NULL) {
        syserr("CreateFileMapping(name=%s, len=%llu)", full, size);
        return -1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Another live process owns this name. (Unlike System V segments, Windows removes an
         * object when its last handle closes, so a crashed run leaves nothing stale behind.) */
        CloseHandle(h);
        SetLastError(ERROR_ALREADY_EXISTS);
        syserr("CreateFileMapping(name=%s) [name already in use]", full);
        return -1;
    }

    addr = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, len);
    if (addr == NULL) {
        syserr("MapViewOfFile(name=%s, len=%llu)", full, size);
        CloseHandle(h);
        return -1;
    }

    shm->addr = addr;
    shm->len = len;
    shm->id = (intptr_t)h;
    shm->owner = 1;
    return 0;
}

int plat_shm_attach(const char *name, size_t len, plat_shm *shm)
{
    char full[256];
    HANDLE h;
    void *addr;

    shm_object_name(name, full, sizeof full);
    h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, full);
    if (h == NULL) {
        syserr("OpenFileMapping(name=%s) [attach existing]", full);
        return -1;
    }
    addr = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, len);
    if (addr == NULL) {
        syserr("MapViewOfFile(name=%s, len=%llu)", full, ull(len));
        CloseHandle(h);
        return -1;
    }

    shm->addr = addr;
    shm->len = len;
    shm->id = (intptr_t)h;
    shm->owner = 0;
    return 0;
}

int plat_shm_detach(plat_shm *shm)
{
    int rc = 0;

    if (shm->addr == NULL) {
        return 0;
    }
    if (!UnmapViewOfFile(shm->addr)) {
        syserr("UnmapViewOfFile(%p)", shm->addr);
        rc = -1;
    }
    shm->addr = NULL;
    if (!shm->owner && shm->id != 0) {
        /* An attaching process holds its own handle; the creator keeps its handle until destroy. */
        CloseHandle((HANDLE)shm->id);
        shm->id = 0;
    }
    return rc;
}

int plat_shm_destroy(plat_shm *shm)
{
    int rc = plat_shm_detach(shm);

    if (shm->owner && shm->id != 0) {
        /* The object disappears once the last handle (ours) is closed. */
        if (!CloseHandle((HANDLE)shm->id)) {
            syserr("CloseHandle(mapping)");
            rc = -1;
        }
        shm->id = 0;
        shm->owner = 0;
    }
    return rc;
}

/* ======================= Permissions: SetFileSecurity ===================== */

int plat_chmod(const char *path, unsigned mode)
{
    return apply_mode(path, mode & 0777u);
}

int plat_chown(const char *path, long uid, long gid)
{
    BYTE admins[SECURITY_MAX_SID_SIZE];
    DWORD admins_size = sizeof admins;
    union {
        TOKEN_USER tu;
        BYTE raw[512];
    } u;
    SECURITY_DESCRIPTOR sd;
    PSID new_owner = NULL;
    HANDLE tok = NULL;
    DWORD len = 0;
    int rc = -1;

    if (gid != -1 && gid != PSEUDO_GID) {
        SetLastError(ERROR_NOT_SUPPORTED);
        syserr("chown(%s, gid=%ld): only the current group id (%d) is supported on Windows", path, gid, PSEUDO_GID);
        return -1;
    }
    if (uid == -1) {
        return 0; /* nothing to change */
    }

    if (uid == 0) {
        /* uid 0 stands for the Administrators group; only a privileged process may assign it. */
        if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admins, &admins_size)) {
            syserr("chown(%s): look up the Administrators SID", path);
            return -1;
        }
        new_owner = (PSID)admins;
    } else if (uid == PSEUDO_UID) {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok) ||
            !GetTokenInformation(tok, TokenUser, &u, sizeof u, &len)) {
            syserr("chown(%s): look up the current user", path);
            if (tok != NULL) {
                CloseHandle(tok);
            }
            return -1;
        }
        CloseHandle(tok);
        new_owner = u.tu.User.Sid;
    } else {
        SetLastError(ERROR_NOT_SUPPORTED);
        syserr("chown(%s, uid=%ld): Windows supports uid 0 (Administrators) and uid %d (current user) only", path, uid,
               PSEUDO_UID);
        return -1;
    }

    if (InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorOwner(&sd, new_owner, FALSE) &&
        SetFileSecurityA(path, OWNER_SECURITY_INFORMATION, &sd)) {
        rc = 0;
    } else {
        syserr("SetFileSecurity(%s, owner=uid %ld)", path, uid);
    }
    return rc;
}

unsigned plat_umask(unsigned mask)
{
    /* Windows has no umask, and the C runtime's _umask() only affects _open/_creat. This emulation
     * applies the mask in plat_open() to files it creates. It stays off until first called. */
    unsigned old = g_umask;

    g_umask = mask & 0777u;
    g_umask_active = 1;
    return old;
}

long plat_getuid(void)
{
    return PSEUDO_UID;
}

long plat_getgid(void)
{
    return PSEUDO_GID;
}
