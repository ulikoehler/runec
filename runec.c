/*
 * runec – setuid helper that grants network capabilities to unprivileged processes
 *
 * Compile-time options:
 *   -DENABLE_CAP_NET_RAW=1     (default: 1)  Grant CAP_NET_RAW
 *   -DENABLE_CAP_NET_ADMIN=1   (default: 1)  Grant CAP_NET_ADMIN
 *   -DENABLE_DEBUG_LOG=0       (default: 0)  Enable verbose logging
 *
 * Build:
 *   gcc -o runec runec.c -lcap
 *   gcc -DENABLE_DEBUG_LOG=1 -o runec runec.c -lcap    # with debug logging
 *   gcc -DENABLE_CAP_NET_ADMIN=0 -o runec runec.c -lcap # NET_RAW only
 *
 * Install:
 *   sudo install -o root -g root -m 4755 runec /usr/local/bin/
 *   sudo setcap cap_net_raw,cap_net_admin+ep /usr/local/bin/runec  # optional
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <linux/prctl.h>

/* ==================== Compile-time Configuration ==================== */

#ifndef ENABLE_CAP_NET_RAW
#define ENABLE_CAP_NET_RAW 1
#endif

#ifndef ENABLE_CAP_NET_ADMIN
#define ENABLE_CAP_NET_ADMIN 1
#endif

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 0
#endif

/* Build the capability list based on what's enabled */
static const cap_value_t REQUIRED_CAPS[] = {
#if ENABLE_CAP_NET_RAW
    CAP_NET_RAW,
#endif
#if ENABLE_CAP_NET_ADMIN
    CAP_NET_ADMIN,
#endif
};
static const int NUM_REQUIRED_CAPS = sizeof(REQUIRED_CAPS) / sizeof(REQUIRED_CAPS[0]);

/* ==================== Helper Macros ==================== */

#if ENABLE_DEBUG_LOG
    #define DEBUG_LOG(fmt, ...) fprintf(stderr, "[runec] " fmt "\n", ##__VA_ARGS__)
    #define DEBUG_DUMP_CAPS(label) dump_caps(label)
#else
    #define DEBUG_LOG(fmt, ...) ((void)0)
    #define DEBUG_DUMP_CAPS(label) ((void)0)
#endif

#define ERROR_LOG(fmt, ...) fprintf(stderr, "[runec] ERROR: " fmt "\n", ##__VA_ARGS__)
#define INFO_LOG(fmt, ...) fprintf(stderr, "[runec] " fmt "\n", ##__VA_ARGS__)

/* ==================== Helper Functions ==================== */

static void die(const char *msg)
{
    ERROR_LOG("%s: %s (errno=%d)", msg, strerror(errno), errno);
    exit(1);
}

static void diemsg(const char *msg)
{
    ERROR_LOG("%s", msg);
    exit(1);
}

#if ENABLE_DEBUG_LOG
/* Print current process capabilities for debugging */
static void dump_caps(const char *label)
{
    cap_t caps = cap_get_proc();
    if (!caps) {
        ERROR_LOG("cap_get_proc() failed: %s", strerror(errno));
        return;
    }
    char *txt = cap_to_text(caps, NULL);
    if (txt) {
        DEBUG_LOG("%s: %s", label, txt);
        cap_free(txt);
    }
    cap_free(caps);
}
#endif

/* Verify a specific capability is in the effective set */
static int have_cap(cap_value_t c)
{
    cap_t caps = cap_get_proc();
    if (!caps) return 0;
    cap_flag_value_t v = CAP_CLEAR;
    cap_get_flag(caps, c, CAP_EFFECTIVE, &v);
    cap_free(caps);
    return v == CAP_SET;
}

/* Check all required capabilities are present */
static int have_all_caps(void)
{
    int i;
    for (i = 0; i < NUM_REQUIRED_CAPS; i++) {
        if (!have_cap(REQUIRED_CAPS[i]))
            return 0;
    }
    return 1;
}

static const char* cap_name(cap_value_t cap)
{
    switch (cap) {
        case CAP_NET_RAW: return "CAP_NET_RAW";
        case CAP_NET_ADMIN: return "CAP_NET_ADMIN";
        default: return "CAP_UNKNOWN";
    }
}

static void print_usage(void)
{
    fprintf(stderr,
        "Usage: runec <executable> [args...]\n"
        "\n"
        "Runs <executable> with elevated network capabilities.\n"
        "\n"
        "Capabilities granted:\n");
    
    int i;
    for (i = 0; i < NUM_REQUIRED_CAPS; i++) {
        fprintf(stderr, "  - %s\n", cap_name(REQUIRED_CAPS[i]));
    }
    
    fprintf(stderr,
        "\n"
        "runec must be installed setuid-root or with matching file capabilities.\n"
        "Install:\n"
        "  sudo chown root:root runec && sudo chmod 4755 runec\n");
}

/* ==================== Main ==================== */

int main(int argc, char *argv[])
{
    /* Compile-time sanity check */
#if !ENABLE_CAP_NET_RAW && !ENABLE_CAP_NET_ADMIN
    #error "At least one capability must be enabled (ENABLE_CAP_NET_RAW or ENABLE_CAP_NET_ADMIN)"
#endif

    /* ---- Argument check ---- */
    if (argc < 2) {
        print_usage();
        return 1;
    }

    char *target = argv[1];

    /* ---- Verify target executable ---- */
    struct stat st;
    if (stat(target, &st) == -1) {
        ERROR_LOG("Cannot stat '%s': %s", target, strerror(errno));
        return 1;
    }
    if (!S_ISREG(st.st_mode)) {
        ERROR_LOG("'%s' is not a regular file", target);
        return 1;
    }
    if (access(target, X_OK) == -1) {
        ERROR_LOG("'%s' is not executable: %s", target, strerror(errno));
        return 1;
    }

    /* ---- Verify runec has necessary privileges ---- */
    uid_t euid = geteuid();
    uid_t ruid = getuid();
    gid_t rgid = getgid();

    DEBUG_LOG("Initial state: euid=%d ruid=%d", euid, ruid);
    DEBUG_DUMP_CAPS("Initial caps");

    if (euid != 0 && !have_all_caps()) {
        diemsg("runec is not running with sufficient privileges.\n"
               "       Install: sudo chown root:root runec && sudo chmod 4755 runec");
    }

    DEBUG_LOG("Privileges verified");
    DEBUG_LOG("Target: %s", target);

    /* ---- Set up capability inheritance ---- */

    /* 1. Keep capabilities across setuid */
    if (prctl(PR_SET_KEEPCAPS, 1L) == -1)
        die("prctl(PR_SET_KEEPCAPS)");

    /* 2. Drop to real user's uid/gid */
    if (setresgid(rgid, rgid, rgid) == -1)
        die("setresgid");
    if (setresuid(ruid, ruid, ruid) == -1)
        die("setresuid");

    DEBUG_LOG("Dropped to uid=%d gid=%d", getuid(), getgid());

    /* 3. Raise required capabilities in permitted, effective, and inheritable sets */
    {
        cap_t caps = cap_init();
        if (!caps) die("cap_init");

        if (cap_set_flag(caps, CAP_PERMITTED, NUM_REQUIRED_CAPS,
                         REQUIRED_CAPS, CAP_SET) == -1)
            die("cap_set_flag PERMITTED");
        if (cap_set_flag(caps, CAP_EFFECTIVE, NUM_REQUIRED_CAPS,
                         REQUIRED_CAPS, CAP_SET) == -1)
            die("cap_set_flag EFFECTIVE");
        if (cap_set_flag(caps, CAP_INHERITABLE, NUM_REQUIRED_CAPS,
                         REQUIRED_CAPS, CAP_SET) == -1)
            die("cap_set_flag INHERITABLE");

        if (cap_set_proc(caps) == -1)
            die("cap_set_proc");
        cap_free(caps);
    }

    DEBUG_DUMP_CAPS("After cap_set_proc");

    /* 4. Set ambient capabilities (survives execve, requires kernel >= 4.3) */
    {
        int i;
        for (i = 0; i < NUM_REQUIRED_CAPS; i++) {
            if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
                      REQUIRED_CAPS[i], 0, 0) == -1) {
                ERROR_LOG("prctl(PR_CAP_AMBIENT_RAISE) failed for %s: %s",
                         cap_name(REQUIRED_CAPS[i]), strerror(errno));
                ERROR_LOG("Your kernel may not support ambient capabilities (need >= 4.3)");
                exit(1);
            }
        }
    }

    DEBUG_DUMP_CAPS("Final caps before exec");

    /* 5. Final verification */
    if (!have_all_caps())
        diemsg("Required capabilities not in effective set — aborting");

    DEBUG_LOG("Launching: %s", target);

    /* ---- Execute target ---- */
    execv(target, argv + 1);

    /* execv only returns on error */
    ERROR_LOG("execv('%s') failed: %s", target, strerror(errno));
    return 1;
}
