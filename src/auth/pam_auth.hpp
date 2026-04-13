#ifndef PAM_AUTH_HPP
#define PAM_AUTH_HPP

#include <string>
#include <vector>
#include <security/pam_appl.h>

namespace greeter {

    // Holds an open PAM session handle and XDG session metadata.
    // Keep alive until the child session process exits, then call closeSession().
    struct PamSession {
        pam_handle_t* handle;    // Active PAM handle — do not call pam_end() prematurely.
        std::string session_id;  // XDG_SESSION_ID from pam_systemd (may be empty).
        std::string runtime_dir; // XDG_RUNTIME_DIR path (e.g. /run/user/1000).
    };

    class PamAuth {
    public:
        // PAM conversation callback — answers PAM_PROMPT_ECHO_OFF with the stored password.
        static int pamConversation(int num_msg, const struct pam_message** msg,
                                   struct pam_response** resp, void* appdata_ptr);

        // Validates credentials via PAM WITHOUT opening a session.
        // Called before the session selector so wrong passwords always loop back here.
        static bool authenticatePassword(const std::string& username,
                                         const std::string& password);

        // Opens a full PAM session (authenticate → acct_mgmt → setcred → open_session).
        // Sets XDG_SESSION_TYPE and XDG_VTNR before opening so pam_systemd registers correctly.
        static PamSession* openSession(const std::string& username,
                                         const std::string& password,
                                         int vt_number,
                                         const std::string& session_type);

        // Closes the PAM session and frees all resources. Safe to call with nullptr.
        static void closeSession(PamSession* session);

        // Returns login-capable system users (UID 1000–65533, excludes nologin/false shells).
        static std::vector<std::string> getSystemUsers();
    };

} // namespace greeter

#endif