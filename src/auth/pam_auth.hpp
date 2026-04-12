#pragma once

#include <string>
#include <vector>
#include <security/pam_appl.h>

namespace greeter {

    // Holds the PAM session handle and associated metadata
    struct PamSession {
        pam_handle_t* handle;
        std::string session_id;
        std::string runtime_dir;
    };

    class PamAuth {
    public:
        PamAuth() = default;
        ~PamAuth() = default;

        // Opens a PAM session and returns a handle (caller is responsible for closing)
        PamSession* openSession(const std::string& username,
                                const std::string& password,
                                int vt_number);

        // Closes the PAM session (call after the child process exits)
        void closeSession(PamSession* session);

        // Returns a list of login-capable system users
        static std::vector<std::string> getSystemUsers();

    private:
        static int pamConversation(int num_msg, const struct pam_message** msg,
                                  struct pam_response** resp, void* appdata_ptr);
    };

} // namespace greeter
