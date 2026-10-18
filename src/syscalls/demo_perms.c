/*
 * demo_perms.c - file permissions.
 *
 * Linux: chmod, chown, umask.
 * Windows: SetFileSecurity (DACL for chmod, owner for chown); umask has no Win32
 * equivalent (see docs/SYSCALL_MAPPING.md).
 *
 *   1. umask: a new file asked to be 0666 is created as 0600 under a mask of 077.
 *   2. chmod: change the mode to 0640 and read it back.
 *   3. chown: changing to your own uid works; changing to someone else needs
 *      privilege. Run as a normal user, the change to root fails with EPERM (an
 *      expected error). Run as root, a change to uid 65534 succeeds instead.
 *   4. A forced error: chmod on a path that does not exist.
 */

#include "demo_util.h"
#include "platform.h"

#define FILE_NAME "sk_perms.tmp"

int main(int argc, char **argv)
{
    int failures = 0;
    plat_stat st;
    plat_handle h;
    unsigned old_mask;
    long uid;
    long gid;

    demo_begin(argc, argv, "perms");

    /* ---- 1. umask ---- */
    old_mask = plat_umask(077);
    DEMO_CHECK(failures, plat_umask(old_mask) == 077, "umask returns the previous mask");
    (void)plat_umask(077);
    h = plat_open(FILE_NAME, PLAT_OPEN_WRITE | PLAT_OPEN_CREATE | PLAT_OPEN_TRUNC, 0666);
    DEMO_CHECK(failures, h != PLAT_INVALID_HANDLE, "created a file asking for mode 0666");
    if (h != PLAT_INVALID_HANDLE) {
        (void)plat_close(h);
    }
    (void)plat_umask(old_mask);
    st.mode = 0;
    (void)plat_stat_path(FILE_NAME, &st);
    LOG_INFO("  mode of the new file is 0%o", st.mode);
    DEMO_CHECK(failures, st.mode == 0600, "under umask 077 the file was created with mode 0600");

    /* ---- 2. chmod ---- */
    DEMO_CHECK(failures, plat_chmod(FILE_NAME, 0640) == 0, "chmod to 0640");
    st.mode = 0;
    (void)plat_stat_path(FILE_NAME, &st);
    LOG_INFO("  mode after chmod is 0%o", st.mode);
    DEMO_CHECK(failures, st.mode == 0640, "mode reads back as 0640");

    /* ---- 3. chown ---- */
    uid = plat_getuid();
    gid = plat_getgid();
    LOG_INFO("running as uid %ld gid %ld", uid, gid);
    DEMO_CHECK(failures, plat_chown(FILE_NAME, uid, gid) == 0, "chown to the current owner succeeds");
    if (uid != 0) {
        LOG_INFO("not root: changing the owner to uid 0 should fail; an ERROR line is expected");
        if (plat_chown(FILE_NAME, 0, -1) == -1) {
            DEMO_CHECK(failures, 1, "chown to another user is refused without privilege");
        } else {
            /* Only happens in an elevated Windows shell, where assigning Administrators is allowed. */
            LOG_INFO("chown to uid 0 succeeded: this process is privileged");
            DEMO_CHECK(failures, plat_stat_path(FILE_NAME, &st) == 0 && st.uid == 0, "owner reads back as uid 0");
            DEMO_CHECK(failures, plat_chown(FILE_NAME, uid, -1) == 0, "gave the file back to the current user");
        }
    } else {
        DEMO_CHECK(failures, plat_chown(FILE_NAME, 65534, -1) == 0, "running as root: chown to uid 65534 succeeds");
        DEMO_CHECK(failures, plat_stat_path(FILE_NAME, &st) == 0 && st.uid == 65534, "owner reads back as 65534");
    }

    /* ---- 4. forced error ---- */
    LOG_INFO("chmod on a missing path; the ERROR line that follows is expected");
    DEMO_CHECK(failures, plat_chmod("sk_no_such_file.tmp", 0600) == -1, "chmod on a missing path fails");

    if (failures == 0) {
        DEMO_CHECK(failures, plat_unlink(FILE_NAME) == 0, "removed the temporary file");
    } else {
        /* Leave the file behind so its permissions can be inspected (Windows: icacls sk_perms.tmp). */
        LOG_WARN("a check failed: left %s in place for inspection", FILE_NAME);
    }
    return demo_finish("perms", failures);
}
