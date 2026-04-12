#include "session_manager.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <linux/vt.h>
#include <linux/kd.h>

namespace greeter {

SessionManager::SessionManager() {
    scanWaylandSessions();
    scanX11Sessions();
}

std::vector<SessionInfo> SessionManager::getAvailableSessions() {
    return sessions_;
}

void SessionManager::scanWaylandSessions() {
    sessions_.push_back({
        "KDE Plasma (Wayland)",
        "dbus-run-session startplasma-wayland",
        "wayland"
    });

    sessions_.push_back({
        "Hyprland",
        "Hyprland",
        "wayland"
    });
}

void SessionManager::scanX11Sessions() {
    // X11 session support is not yet implemented
}

bool SessionManager::startSession(const std::string& username,
                                   const SessionInfo& session,
                                   const std::string& session_id,
                                   const std::string& runtime_dir,
                                   int vt_number) {
    // Look up the user's account information
    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) {
        return false;
    }

    uid_t user_uid = pw->pw_uid;
    gid_t user_gid = pw->pw_gid;
    const char* user_home = pw->pw_dir;
    const char* user_shell = pw->pw_shell;

    // Open the target VT in the parent process before forking (mirrors SDDM behaviour)
    char tty_path[32];
    sprintf(tty_path, "/dev/tty%d", vt_number);

    FILE* sm_log = fopen("/tmp/session-manager-parent.log", "w");
    fprintf(sm_log, "PARENT: Opening VT: %s\n", tty_path);
    fflush(sm_log);

    int vt_fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (vt_fd < 0) {
        fprintf(sm_log, "PARENT: Failed to open VT: %s\n", strerror(errno));
        fclose(sm_log);
        return false;
    }

    fprintf(sm_log, "PARENT: VT opened successfully, fd=%d\n", vt_fd);
    fclose(sm_log);

    pid_t pid = fork();

    if (pid < 0) {
        close(vt_fd);
        return false;
    }

    if (pid == 0) {
        // ═══ CHILD PROCESS ═══

        // Redirect stderr to a log file for debugging
        int logfd = open("/tmp/greeter-child.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, 2);
            close(logfd);
        }

        fprintf(stderr, "=== Child Process ===\n"); fflush(stderr);
        fprintf(stderr, "PID: %d, PPID: %d\n", getpid(), getppid()); fflush(stderr);
        fprintf(stderr, "User: %s (UID=%d, GID=%d)\n", username.c_str(), user_uid, user_gid); fflush(stderr);
        fprintf(stderr, "VT: %d\n", vt_number); fflush(stderr);
        fprintf(stderr, "Inherited VT fd: %d\n", vt_fd); fflush(stderr);

        // Wire the inherited VT fd to stdin
        fprintf(stderr, "Redirecting inherited VT fd to stdin...\n"); fflush(stderr);
        dup2(vt_fd, STDIN_FILENO);
        close(vt_fd);
        fprintf(stderr, "VT redirected to stdin\n"); fflush(stderr);

        // Become a session leader
        fprintf(stderr, "Calling setsid()...\n"); fflush(stderr);
        pid_t sid = setsid();
        fprintf(stderr, "setsid() returned: %d (errno: %d, %s)\n", sid, errno, strerror(errno)); fflush(stderr);

        if (sid < 0) {
            fprintf(stderr, "ERROR: setsid() failed\n"); fflush(stderr);
            exit(1);
        }
        fprintf(stderr, "setsid() OK, SID=%d\n", sid); fflush(stderr);

        // Acquire the controlling terminal
        fprintf(stderr, "Calling TIOCSCTTY...\n"); fflush(stderr);
        if (ioctl(STDIN_FILENO, TIOCSCTTY, 0) < 0) {
            fprintf(stderr, "ERROR: TIOCSCTTY: %s (errno: %d)\n", strerror(errno), errno); fflush(stderr);
            exit(1);
        }
        fprintf(stderr, "TIOCSCTTY OK!\n"); fflush(stderr);

        // Switch to the allocated VT
        fprintf(stderr, "Opening /dev/tty0 for VT control...\n"); fflush(stderr);
        int tty0 = open("/dev/tty0", O_RDWR);
        if (tty0 >= 0) {
            fprintf(stderr, "VT_ACTIVATE %d\n", vt_number); fflush(stderr);
            if (ioctl(tty0, VT_ACTIVATE, vt_number) < 0) {
                fprintf(stderr, "WARNING: VT_ACTIVATE: %s\n", strerror(errno)); fflush(stderr);
            } else {
                fprintf(stderr, "VT_ACTIVATE OK\n"); fflush(stderr);
            }

            fprintf(stderr, "VT_WAITACTIVE %d\n", vt_number); fflush(stderr);
            if (ioctl(tty0, VT_WAITACTIVE, vt_number) < 0) {
                fprintf(stderr, "WARNING: VT_WAITACTIVE: %s\n", strerror(errno)); fflush(stderr);
            } else {
                fprintf(stderr, "VT_WAITACTIVE OK\n"); fflush(stderr);
            }
            close(tty0);
        }

        // Drop root privileges
        fprintf(stderr, "setgid(%d)...\n", user_gid); fflush(stderr);
        if (setgid(user_gid) != 0) {
            fprintf(stderr, "ERROR: setgid: %s\n", strerror(errno)); fflush(stderr);
            exit(1);
        }

        fprintf(stderr, "initgroups...\n"); fflush(stderr);
        if (initgroups(username.c_str(), user_gid) != 0) {
            fprintf(stderr, "WARNING: initgroups: %s\n", strerror(errno)); fflush(stderr);
        }

        fprintf(stderr, "setuid(%d)...\n", user_uid); fflush(stderr);
        if (setuid(user_uid) != 0) {
            fprintf(stderr, "ERROR: setuid: %s\n", strerror(errno)); fflush(stderr);
            exit(1);
        }
        fprintf(stderr, "Privilege drop OK, UID=%d\n", getuid()); fflush(stderr);

        // Set up a clean environment for the user session
        fprintf(stderr, "Setting environment...\n"); fflush(stderr);
        clearenv();
        setenv("HOME", user_home, 1);
        setenv("USER", username.c_str(), 1);
        setenv("LOGNAME", username.c_str(), 1);
        setenv("SHELL", user_shell, 1);
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        setenv("XDG_RUNTIME_DIR", runtime_dir.c_str(), 1);
        if (!session_id.empty()) {
            setenv("XDG_SESSION_ID", session_id.c_str(), 1);
        }
        setenv("XDG_SESSION_TYPE", session.type.c_str(), 1);
        setenv("XDG_SESSION_CLASS", "user", 1);
        setenv("XDG_SEAT", "seat0", 1);

        char vt_str[16];
        sprintf(vt_str, "%d", vt_number);
        setenv("XDG_VTNR", vt_str, 1);
        setenv("XDG_CURRENT_DESKTOP", "KDE", 1);

        fprintf(stderr, "Environment OK\n"); fflush(stderr);

        if (chdir(user_home) != 0) {
            fprintf(stderr, "WARNING: chdir failed\n"); fflush(stderr);
            chdir("/");
        }

        fprintf(stderr, "Executing: %s\n", session.exec_command.c_str()); fflush(stderr);

        execl("/bin/sh", "sh", "-c", session.exec_command.c_str(), nullptr);

        fprintf(stderr, "ERROR: exec failed: %s\n", strerror(errno)); fflush(stderr);
        exit(1);
    }

    // ═══ PARENT PROCESS ═══
    // Keep the VT fd open until the child exits to prevent getty from racing
    // to reclaim the terminal.

    return true;
}
} // namespace greeter
