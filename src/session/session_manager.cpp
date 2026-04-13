// Session manager implementation — discovers .desktop sessions and launches them.
// Author: tambiyusuf
//
// Known issues:
//   - X11 sessions are discovered but launch unreliably (no Xorg startup logic).
//   - dbus-run-session wrapper may not be appropriate for all session types.
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
#include <dirent.h>
#include <fstream>
#include <sstream>

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

// Parses a freedesktop .desktop session file and extracts Name and Exec fields.
SessionInfo SessionManager::parseDesktopFile(const std::string& filepath, const std::string& type) {
    SessionInfo info;
    info.type = type;

    std::ifstream file(filepath);
    if (!file.is_open()) {
        return info;
    }

    std::string line;
    bool in_desktop_entry = false;

    while (std::getline(file, line)) {
        // Skip empty lines and comments.
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Locate the [Desktop Entry] section.
        if (line == "[Desktop Entry]") {
            in_desktop_entry = true;
            continue;
        }

        // Stop parsing when a new section begins.
        if (line[0] == '[' && in_desktop_entry) {
            break;
        }

        if (!in_desktop_entry) {
            continue;
        }

        // Parse Key=Value pairs.
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Trim leading/trailing whitespace from key and value.
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        if (key == "Name") {
            info.name = value;
        } else if (key == "Exec") {
            info.exec_command = value;
        } else if (key == "Type" && value != "Application") {
            // Type Application olmalı, değilse geçersiz
            info.name.clear();
            break;
        }
    }

    file.close();
    return info;
}

void SessionManager::scanWaylandSessions() {
    const char* wayland_dir = "/usr/share/wayland-sessions";

    FILE* log = fopen("/tmp/session-scan-wayland.log", "w");
    fprintf(log, "=== Scanning Wayland Sessions ===\n");
    fprintf(log, "Directory: %s\n", wayland_dir);

    DIR* dir = opendir(wayland_dir);
    if (!dir) {
        fprintf(log, "Failed to open directory: %s\n", strerror(errno));
        fprintf(log, "Falling back to hardcoded sessions\n");
        fclose(log);

        // Fallback: hardcoded sessions
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
        return;
    }

    struct dirent* entry;
    int count = 0;

    while ((entry = readdir(dir)) != nullptr) {
        // Only process .desktop files.
        std::string filename = entry->d_name;
        if (filename.length() < 8 || filename.substr(filename.length() - 8) != ".desktop") {
            continue;
        }

        std::string filepath = std::string(wayland_dir) + "/" + filename;
        fprintf(log, "Found: %s\n", filepath.c_str());

        SessionInfo session = parseDesktopFile(filepath, "wayland");

        if (!session.name.empty() && !session.exec_command.empty()) {
            fprintf(log, "  Name: %s\n", session.name.c_str());
            fprintf(log, "  Exec: %s\n", session.exec_command.c_str());
            fprintf(log, "  Type: %s\n", session.type.c_str());

            sessions_.push_back(session);
            count++;
        } else {
            fprintf(log, "  Skipped (invalid or incomplete)\n");
        }
    }

    closedir(dir);

    fprintf(log, "Total Wayland sessions found: %d\n", count);
    fclose(log);

    // No .desktop files found — fall back to hardcoded Wayland sessions.
    if (count == 0) {
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
}

void SessionManager::scanX11Sessions() {
    const char* x11_dir = "/usr/share/xsessions";

    FILE* log = fopen("/tmp/session-scan-x11.log", "w");
    fprintf(log, "=== Scanning X11 Sessions ===\n");
    fprintf(log, "Directory: %s\n", x11_dir);

    DIR* dir = opendir(x11_dir);
    if (!dir) {
        fprintf(log, "Failed to open directory: %s\n", strerror(errno));
        fprintf(log, "No X11 sessions available\n");
        fclose(log);
        return;
    }

    struct dirent* entry;
    int count = 0;

    while ((entry = readdir(dir)) != nullptr) {
        // Only process .desktop files.
        std::string filename = entry->d_name;
        if (filename.length() < 8 || filename.substr(filename.length() - 8) != ".desktop") {
            continue;
        }

        std::string filepath = std::string(x11_dir) + "/" + filename;
        fprintf(log, "Found: %s\n", filepath.c_str());

        SessionInfo session = parseDesktopFile(filepath, "x11");

        if (!session.name.empty() && !session.exec_command.empty()) {
            fprintf(log, "  Name: %s\n", session.name.c_str());
            fprintf(log, "  Exec: %s\n", session.exec_command.c_str());
            fprintf(log, "  Type: %s\n", session.type.c_str());

            sessions_.push_back(session);
            count++;
        } else {
            fprintf(log, "  Skipped (invalid or incomplete)\n");
        }
    }

    closedir(dir);

    fprintf(log, "Total X11 sessions found: %d\n", count);
    fclose(log);
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

    // First fork — creates an intermediate process so the greeter can return immediately.
    pid_t intermediate_pid = fork();

    if (intermediate_pid < 0) {
        close(vt_fd);
        return false;
    }

    if (intermediate_pid > 0) {
        // Greeter process — return true and let systemd restart the greeter service.
        close(vt_fd);
        return true;
    }

    // === INTERMEDIATE PROCESS ===

    // Detach from the greeter's session so the child outlives it.
    setsid();

    signal(SIGTERM, intermediate_signal_handler);
    signal(SIGHUP, intermediate_signal_handler);

    FILE* intermediate_log = fopen("/tmp/session-intermediate.log", "w");
    fprintf(intermediate_log, "=== Intermediate Process ===\n");
    fprintf(intermediate_log, "PID: %d, PPID: %d, SID: %d\n", getpid(), getppid(), getsid(0));
    fflush(intermediate_log);

    // Second fork — the actual session process; intermediate waits for it to exit.
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

        // Wrap with dbus-run-session unless the command already manages its own D-Bus session.
        bool needs_dbus_wrapper = true;

        // Skip the wrapper for commands that bring their own session/dbus manager.
        if (session.exec_command.find("uwsm") == 0 ||
            session.exec_command.find("systemd-run") == 0 ||
            session.exec_command.find("/usr/lib/") == 0 ||
            session.exec_command.find("dbus-run-session") != std::string::npos ||
            session.exec_command.find("dbus-launch") != std::string::npos) {
            needs_dbus_wrapper = false;
        }

        if (needs_dbus_wrapper) {
            exec_cmd = "dbus-run-session " + session.exec_command;
        } else {
            exec_cmd = session.exec_command;
        }

        fprintf(child_log, "Executing: %s\n", exec_cmd.c_str());
        fprintf(child_log, "DBus wrapper needed: %s\n", needs_dbus_wrapper ? "yes" : "no");
        fflush(child_log);
        fclose(child_log);

        execl("/bin/sh", "sh", "-c", exec_cmd.c_str(), nullptr);

        // execl() only returns on failure.
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

    // Switch back to VT1 so the greeter (restarted by systemd) is visible.
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