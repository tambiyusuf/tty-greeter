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
#include <signal.h>

namespace greeter {

// Signal handler for intermediate process
static void intermediate_signal_handler(int sig) {
    FILE* f = fopen("/tmp/intermediate-signal.log", "a");
    if (f) {
        fprintf(f, "Received signal %d! Exiting...\n", sig);
        fclose(f);
    }
    _exit(0);
}

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
        "startplasma-wayland",
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

    FILE* log = fopen("/tmp/session-manager-parent.log", "w");
    fprintf(log, "=== Session Manager (Parent) ===\n");

    char vt_path[32];
    snprintf(vt_path, sizeof(vt_path), "/dev/tty%d", vt_number);

    int vt_fd = open(vt_path, O_RDWR | O_NOCTTY);
    if (vt_fd < 0) {
        fprintf(log, "Failed to open %s: %s\n", vt_path, strerror(errno));
        fclose(log);
        return false;
    }

    fprintf(log, "Opened VT fd: %d for %s\n", vt_fd, vt_path);
    fflush(log);
    fclose(log);

    // FIRST FORK - Intermediate process
    pid_t intermediate_pid = fork();

    if (intermediate_pid < 0) {
        close(vt_fd);
        return false;
    }

    if (intermediate_pid > 0) {
        // GREETER - hemen return
        close(vt_fd);
        return true;
    }

    // === INTERMEDIATE PROCESS ===

    // Systemd'den ayrıl
    setsid();

    signal(SIGTERM, intermediate_signal_handler);
    signal(SIGHUP, intermediate_signal_handler);

    FILE* intermediate_log = fopen("/tmp/session-intermediate.log", "w");
    fprintf(intermediate_log, "=== Intermediate Process ===\n");
    fprintf(intermediate_log, "PID: %d, PPID: %d, SID: %d\n", getpid(), getppid(), getsid(0));
    fflush(intermediate_log);

    // SECOND FORK - Session process
    pid_t session_pid = fork();

    if (session_pid < 0) {
        fprintf(intermediate_log, "Session fork failed: %s\n", strerror(errno));
        fflush(intermediate_log);
        fclose(intermediate_log);
        close(vt_fd);
        _exit(1);
    }

    if (session_pid == 0) {
        // === SESSION PROCESS ===
        fclose(intermediate_log);

        FILE* child_log = fopen("/tmp/greeter-child.log", "w");
        fprintf(child_log, "=== Child Process ===\n");
        fprintf(child_log, "PID: %d, PPID: %d\n", getpid(), getppid());

        struct passwd* pw = getpwnam(username.c_str());
        fprintf(child_log, "User: %s (UID=%d, GID=%d)\n",
                username.c_str(), pw->pw_uid, pw->pw_gid);
        fprintf(child_log, "VT: %d\n", vt_number);
        fprintf(child_log, "Inherited VT fd: %d\n", vt_fd);
        fflush(child_log);

        // VT setup
        fprintf(child_log, "Redirecting inherited VT fd to stdin...\n");
        dup2(vt_fd, STDIN_FILENO);
        dup2(vt_fd, STDOUT_FILENO);
        dup2(vt_fd, STDERR_FILENO);
        if (vt_fd > 2) close(vt_fd);
        fprintf(child_log, "VT redirected to stdin\n");
        fflush(child_log);

        // Session leader
        fprintf(child_log, "Calling setsid()...\n");
        pid_t sid = setsid();
        fprintf(child_log, "setsid() returned: %d (errno: %d, %s)\n",
                sid, errno, strerror(errno));
        fflush(child_log);

        if (sid < 0) {
            fprintf(child_log, "setsid() failed!\n");
            fclose(child_log);
            _exit(1);
        }
        fprintf(child_log, "setsid() OK, SID=%d\n", sid);

        // Controlling TTY
        fprintf(child_log, "Calling TIOCSCTTY...\n");
        if (ioctl(STDIN_FILENO, TIOCSCTTY, 1) < 0) {
            fprintf(child_log, "TIOCSCTTY failed: %s\n", strerror(errno));
            fclose(child_log);
            _exit(1);
        }
        fprintf(child_log, "TIOCSCTTY OK!\n");

        // VT activate
        fprintf(child_log, "Opening /dev/tty0 for VT control...\n");
        int tty0 = open("/dev/tty0", O_RDWR);
        if (tty0 >= 0) {
            fprintf(child_log, "VT_ACTIVATE %d\n", vt_number);
            ioctl(tty0, VT_ACTIVATE, vt_number);
            fprintf(child_log, "VT_ACTIVATE OK\n");

            fprintf(child_log, "VT_WAITACTIVE %d\n", vt_number);
            ioctl(tty0, VT_WAITACTIVE, vt_number);
            fprintf(child_log, "VT_WAITACTIVE OK\n");
            close(tty0);
        }

        // Drop privileges
        fprintf(child_log, "setgid(%d)...\n", pw->pw_gid);
        setgid(pw->pw_gid);
        fprintf(child_log, "initgroups...\n");
        initgroups(pw->pw_name, pw->pw_gid);
        fprintf(child_log, "setuid(%d)...\n", pw->pw_uid);
        setuid(pw->pw_uid);
        fprintf(child_log, "Privilege drop OK, UID=%d\n", getuid());

        // Environment
        fprintf(child_log, "Setting environment...\n");
        setenv("HOME", pw->pw_dir, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("SHELL", pw->pw_shell, 1);
        setenv("XDG_RUNTIME_DIR", runtime_dir.c_str(), 1);
        setenv("XDG_SESSION_TYPE", session.type.c_str(), 1);
        setenv("XDG_SESSION_CLASS", "user", 1);

        if (!session_id.empty()) {
            setenv("XDG_SESSION_ID", session_id.c_str(), 1);
        }

        char vt_str[16];
        snprintf(vt_str, sizeof(vt_str), "%d", vt_number);
        setenv("XDG_VTNR", vt_str, 1);

        fprintf(child_log, "Environment OK\n");

        // Exec session
        std::string exec_cmd;
        if (session.type == "wayland") {
            exec_cmd = "dbus-run-session " + session.exec_command;
        } else if (session.type == "x11") {
            exec_cmd = "/bin/sh /tmp/.xinitrc-greeter";
        }

        fprintf(child_log, "Executing: %s\n", exec_cmd.c_str());
        fflush(child_log);
        fclose(child_log);

        execl("/bin/sh", "sh", "-c", exec_cmd.c_str(), nullptr);

        // Exec failed
        FILE* err_log = fopen("/tmp/greeter-child.log", "a");
        fprintf(err_log, "exec failed: %s\n", strerror(errno));
        fclose(err_log);
        _exit(1);
    }

    // === INTERMEDIATE PROCESS - Wait for session ===
    close(vt_fd); // Child inherited it

    fprintf(intermediate_log, "Waiting for session process (PID %d)...\n", session_pid);
    fflush(intermediate_log);

    int status;
    waitpid(session_pid, &status, 0);

    fprintf(intermediate_log, "Session exited with status: %d\n", WEXITSTATUS(status));
    fflush(intermediate_log);

    fprintf(intermediate_log, "Switching back to VT1...\n");
    fflush(intermediate_log);

    // VT1'e dön (greeter için)
    int tty0 = open("/dev/tty0", O_RDWR);
    if (tty0 >= 0) {
        fprintf(intermediate_log, "tty0 opened successfully (fd=%d)\n", tty0);
        fflush(intermediate_log);

        int ret = ioctl(tty0, VT_ACTIVATE, 1);
        fprintf(intermediate_log, "VT_ACTIVATE 1 returned: %d (errno: %s)\n", ret, strerror(errno));
        fflush(intermediate_log);

        ret = ioctl(tty0, VT_WAITACTIVE, 1);
        fprintf(intermediate_log, "VT_WAITACTIVE 1 returned: %d (errno: %s)\n", ret, strerror(errno));
        fflush(intermediate_log);

        close(tty0);
        fprintf(intermediate_log, "VT switch to 1 complete\n");
        fflush(intermediate_log);
    } else {
        fprintf(intermediate_log, "Failed to open tty0: %s\n", strerror(errno));
        fflush(intermediate_log);
    }

    fprintf(intermediate_log, "Intermediate process exiting\n");
    fflush(intermediate_log);
    fclose(intermediate_log);

    _exit(0);
}

} // namespace greeter