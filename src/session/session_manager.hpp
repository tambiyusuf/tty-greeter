// Session discovery and launch manager for the custom Wayland greeter.
// Author: tambiyusuf
//
// Known issues:
//   - X11 session launch is unreliable; only Wayland sessions are tested.
//   - No display server (Xorg) startup is handled here — X11 exec commands
//     are passed directly and may fail without additional setup.

#pragma once

#include <string>
#include <vector>

namespace greeter {

    // Metadata parsed from a .desktop session file.
    struct SessionInfo {
        std::string name;         // Display name (e.g. "KDE Plasma (Wayland)").
        std::string exec_command; // Command to exec (may include dbus-run-session wrapper).
        std::string type;         // XDG session type: "wayland" or "x11".
    };

    class SessionManager {
    public:
        // Scans /usr/share/wayland-sessions and /usr/share/xsessions on construction.
        SessionManager();

        std::vector<SessionInfo> getAvailableSessions();

        // Launches the selected session with the provided PAM credentials.
        // Uses a double-fork so the greeter can exit immediately while the session runs.
        bool startSession(const std::string& username,
                         const SessionInfo& session,
                         const std::string& session_id,
                         const std::string& runtime_dir,
                         int vt_number);

    private:
        void scanWaylandSessions();
        void scanX11Sessions();

        // Parses a .desktop file and returns a SessionInfo (name + exec only).
        SessionInfo parseDesktopFile(const std::string& filepath, const std::string& type);

        std::vector<SessionInfo> sessions_;
    };

} // namespace greeter