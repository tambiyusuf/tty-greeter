#ifndef PAM_AUTH_HPP
#define PAM_AUTH_HPP

#include <string>
#include <vector>
#include <security/pam_appl.h>

namespace greeter {

    struct PamSession {
        pam_handle_t* handle;
        std::string session_id;
        std::string runtime_dir;
    };

    class PamAuth {
    public:
        static int pamConversation(int num_msg, const struct pam_message** msg,
                                   struct pam_response** resp, void* appdata_ptr);

        static PamSession* openSession(const std::string& username,
                                         const std::string& password,
                                         int vt_number,
                                         const std::string& session_type);

        static void closeSession(PamSession* session);

        static std::vector<std::string> getSystemUsers();
    };

} // namespace greeter

#endif