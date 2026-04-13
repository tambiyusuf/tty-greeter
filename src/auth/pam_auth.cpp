#include "pam_auth.hpp"
#include <pwd.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>

namespace greeter {

// Validates credentials only — no PAM session is opened.
// Auth check runs before session selection so wrong passwords loop back immediately.
bool PamAuth::authenticatePassword(const std::string& username,
                                    const std::string& password) {
    FILE* auth_debug = fopen("/tmp/pam-password-check.log", "w");
    fprintf(auth_debug, "=== PAM Password Check (No Session) ===\n");
    fprintf(auth_debug, "Username: %s\n", username.c_str());
    fprintf(auth_debug, "Password length: %zu\n", password.length());
    fflush(auth_debug);

    pam_handle_t* pamh = nullptr;
    struct pam_conv conv = {
        pamConversation,
        (void*)password.c_str()
    };

    // Initialize PAM with the "login" service.
    int retval = pam_start("login", username.c_str(), &conv, &pamh);
    fprintf(auth_debug, "pam_start: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);
    if (retval != PAM_SUCCESS) {
        fclose(auth_debug);
        return false;
    }

    // Password-only check — no session side effects.
    retval = pam_authenticate(pamh, 0);
    fprintf(auth_debug, "pam_authenticate: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);

    bool success = (retval == PAM_SUCCESS);

    if (success) {
        // Verify account status (expiry, lockout, etc.).
        retval = pam_acct_mgmt(pamh, 0);
        fprintf(auth_debug, "pam_acct_mgmt: %d (%s)\n", retval, pam_strerror(pamh, retval));
        fflush(auth_debug);

        success = (retval == PAM_SUCCESS);
    }

    // No session opened — just clean up and return the result.
    pam_end(pamh, retval);

    fprintf(auth_debug, "Password check result: %s\n", success ? "SUCCESS" : "FAILED");
    fclose(auth_debug);

    return success;
}

int PamAuth::pamConversation(int num_msg, const struct pam_message** msg,
                             struct pam_response** resp, void* appdata_ptr) {
    const char* password = static_cast<const char*>(appdata_ptr);

    *resp = (struct pam_response*)calloc(num_msg, sizeof(struct pam_response));
    if (*resp == nullptr) {
        return PAM_BUF_ERR;
    }

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            (*resp)[i].resp = strdup(password);
            (*resp)[i].resp_retcode = 0;
        } else {
            (*resp)[i].resp = nullptr;
            (*resp)[i].resp_retcode = 0;
        }
    }

    return PAM_SUCCESS;
}

PamSession* PamAuth::openSession(const std::string& username,
                                  const std::string& password,
                                  int vt_number,
                                  const std::string& session_type) {
    // DEBUG
    FILE* auth_debug = fopen("/tmp/pam-auth-debug.log", "w");
    fprintf(auth_debug, "=== PAM Auth Debug ===\n");
    fprintf(auth_debug, "Username: %s\n", username.c_str());
    fprintf(auth_debug, "Password length: %zu\n", password.length());
    fprintf(auth_debug, "VT: %d\n", vt_number);
    fprintf(auth_debug, "Session type: %s\n", session_type.c_str());
    fflush(auth_debug);

    pam_handle_t* pamh = nullptr;
    struct pam_conv conv = {
        pamConversation,
        (void*)password.c_str()
    };

    // Initialize PAM with the "login" service.
    int retval = pam_start("login", username.c_str(), &conv, &pamh);
    fprintf(auth_debug, "pam_start: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);
    if (retval != PAM_SUCCESS) {
        fclose(auth_debug);
        return nullptr;
    }

    // Set PAM_TTY so pam_systemd can register the correct seat/VT.
    char tty_name[32];
    snprintf(tty_name, sizeof(tty_name), "tty%d", vt_number);
    retval = pam_set_item(pamh, PAM_TTY, tty_name);
    fprintf(auth_debug, "pam_set_item(PAM_TTY, %s): %d (%s)\n",
            tty_name, retval, pam_strerror(pamh, retval));
    fflush(auth_debug);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        fclose(auth_debug);
        return nullptr;
    }

    // Authenticate
    retval = pam_authenticate(pamh, 0);
    fprintf(auth_debug, "pam_authenticate: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        fclose(auth_debug);
        return nullptr;
    }

    // Account management
    retval = pam_acct_mgmt(pamh, 0);
    fprintf(auth_debug, "pam_acct_mgmt: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        fclose(auth_debug);
        return nullptr;
    }

    // Set XDG session environment before opening session so pam_systemd picks them up.
    std::string xdg_type = "XDG_SESSION_TYPE=" + session_type;
    pam_putenv(pamh, xdg_type.c_str());
    pam_putenv(pamh, "XDG_SESSION_CLASS=user");
    pam_putenv(pamh, "XDG_SEAT=seat0");

    char vt_str[32];
    snprintf(vt_str, sizeof(vt_str), "XDG_VTNR=%d", vt_number);
    pam_putenv(pamh, vt_str);

    fprintf(auth_debug, "PAM environment variables set (type: %s)\n", session_type.c_str());
    fflush(auth_debug);

    // Credentials
    retval = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    fprintf(auth_debug, "pam_setcred: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        fclose(auth_debug);
        return nullptr;
    }

    // Open the PAM session — this triggers pam_systemd to register the logind session.
    retval = pam_open_session(pamh, 0);
    fprintf(auth_debug, "pam_open_session: %d (%s)\n", retval, pam_strerror(pamh, retval));
    fflush(auth_debug);

    if (retval != PAM_SUCCESS) {
        fprintf(auth_debug, "ERROR: pam_open_session failed!\n");
        pam_setcred(pamh, PAM_DELETE_CRED);
        pam_end(pamh, retval);
        fclose(auth_debug);
        return nullptr;
    }

    fprintf(auth_debug, "pam_open_session SUCCESS!\n");
    fflush(auth_debug);

    // Extract XDG_SESSION_ID and XDG_RUNTIME_DIR from the PAM environment list.
    char** pam_env = pam_getenvlist(pamh);

    PamSession* session = new PamSession();
    session->handle = pamh;
    session->session_id = "";
    session->runtime_dir = "";

    // Debug
    FILE* env_debug = fopen("/tmp/pam-env-debug.log", "w");

    if (pam_env) {
        for (char** env = pam_env; *env; env++) {
            fprintf(env_debug, "PAM ENV: %s\n", *env);

            if (strncmp(*env, "XDG_SESSION_ID=", 15) == 0) {
                session->session_id = *env + 15;
            } else if (strncmp(*env, "XDG_RUNTIME_DIR=", 16) == 0) {
                session->runtime_dir = *env + 16;
            }
            free(*env);
        }
        free(pam_env);
    }

    fclose(env_debug);

    // If pam_systemd did not provide a runtime dir, fall back to /run/user/<uid>.
    if (session->runtime_dir.empty()) {
        char runtime[256];
        struct passwd* pw = getpwnam(username.c_str());
        snprintf(runtime, sizeof(runtime), "/run/user/%d", pw->pw_uid);
        session->runtime_dir = runtime;

        fprintf(auth_debug, "Runtime dir not from PAM, using: %s\n", runtime);

        // Create the directory if it does not exist yet.
        mkdir(runtime, 0700);
        chown(runtime, pw->pw_uid, pw->pw_gid);
    }

    fprintf(auth_debug, "Session ID: %s\n", session->session_id.c_str());
    fprintf(auth_debug, "Runtime Dir: %s\n", session->runtime_dir.c_str());
    fclose(auth_debug);

    return session;
}

void PamAuth::closeSession(PamSession* session) {
    if (!session) return;

    pam_close_session(session->handle, 0);
    pam_setcred(session->handle, PAM_DELETE_CRED);
    pam_end(session->handle, PAM_SUCCESS);

    delete session;
}

std::vector<std::string> PamAuth::getSystemUsers() {
    std::vector<std::string> users;

    setpwent();
    struct passwd* pw;

    while ((pw = getpwent()) != nullptr) {
        if (pw->pw_uid >= 1000 && pw->pw_uid < 65534) {
            std::string shell = pw->pw_shell;
            if (shell.find("nologin") == std::string::npos &&
                shell.find("false") == std::string::npos) {
                users.push_back(pw->pw_name);
            }
        }
    }

    endpwent();
    return users;
}

} // namespace greeter