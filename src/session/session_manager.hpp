#pragma once

#include <string>
#include <vector>

namespace greeter {

    struct SessionInfo {
        std::string name;
        std::string exec_command;
        std::string type;
    };

    class SessionManager {
    public:
        SessionManager();

        std::vector<SessionInfo> getAvailableSessions();

        // Launches the selected session with the provided PAM credentials
        bool startSession(const std::string& username,
                         const SessionInfo& session,
                         const std::string& session_id,
                         const std::string& runtime_dir,
                         int vt_number);

    private:
        void scanWaylandSessions();
        void scanX11Sessions();

        std::vector<SessionInfo> sessions_;
    };

} // namespace greeter
