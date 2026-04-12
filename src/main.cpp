#include "ui/menu.hpp"
#include "auth/pam_auth.hpp"
#include "session/session_manager.hpp"
#include <cstdlib>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/vt.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <signal.h>

using namespace greeter;

// VT numarası bul
int allocateVT() {
    FILE* log = fopen("/tmp/main-full.log", "a");
    fprintf(log, "[allocateVT] Starting\n");
    fflush(log);

    int tty0 = open("/dev/tty0", O_RDWR);
    if (tty0 < 0) {
        fprintf(log, "[allocateVT] Failed to open /dev/tty0: %s, returning 2\n", strerror(errno));
        fclose(log);
        return 2;
    }

    int vt_num;
    if (ioctl(tty0, VT_OPENQRY, &vt_num) < 0) {
        fprintf(log, "[allocateVT] VT_OPENQRY failed: %s, returning 2\n", strerror(errno));
        close(tty0);
        fclose(log);
        return 2;
    }

    close(tty0);
    fprintf(log, "[allocateVT] Allocated VT: %d\n", vt_num);
    fclose(log);
    return vt_num;
}

int main() {
    // Signal handling - greeter kapanırsa child session devam etsin
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    FILE* log = fopen("/tmp/main-full.log", "w");
    fprintf(log, "[main] Started\n");
    fprintf(log, "[main] PID: %d, PPID: %d\n", getpid(), getppid());
    fprintf(log, "[main] SID before: %d\n", getsid(0));
    fflush(log);

    fprintf(log, "[main] Creating Menu\n");
    fflush(log);
    Menu menu;
    fprintf(log, "[main] Menu created\n");
    fflush(log);

    fprintf(log, "[main] Creating PamAuth\n");
    fflush(log);
    PamAuth auth;
    fprintf(log, "[main] PamAuth created\n");
    fflush(log);

    fprintf(log, "[main] Creating SessionManager\n");
    fflush(log);
    SessionManager sessionMgr;
    fprintf(log, "[main] SessionManager created\n");
    fflush(log);

    fclose(log);

    while (true) {
        log = fopen("/tmp/main-full.log", "a");
        fprintf(log, "[main] Loop iteration start\n");
        fflush(log);

        // 1. User selection
        fprintf(log, "[main] Getting system users\n");
        fflush(log);
        auto users = PamAuth::getSystemUsers();
        fprintf(log, "[main] Got %zu users\n", users.size());
        fflush(log);

        users.push_back("---");
        users.push_back("Shutdown");
        users.push_back("Reboot");
        users.push_back("Exit to TTY");

        fprintf(log, "[main] Calling selectFromList for users\n");
        fflush(log);
        fclose(log);

        int user_choice = menu.selectFromList("Select User", users);

        log = fopen("/tmp/main-full.log", "a");
        fprintf(log, "[main] User choice: %d\n", user_choice);
        fflush(log);

        if (user_choice == -1 || users[user_choice] == "Exit to TTY") {
            fprintf(log, "[main] Exiting\n");
            fclose(log);
            return 0;
        }

        if (users[user_choice] == "Shutdown") {
            fprintf(log, "[main] Shutdown selected\n");
            fclose(log);
            system("systemctl poweroff");
            return 0;
        }

        if (users[user_choice] == "Reboot") {
            fprintf(log, "[main] Reboot selected\n");
            fclose(log);
            system("systemctl reboot");
            return 0;
        }

        fprintf(log, "[main] Selected user: %s\n", users[user_choice].c_str());
        fflush(log);
        std::string selected_user = users[user_choice];

        // 2. Password
        fprintf(log, "[main] Calling getPassword\n");
        fflush(log);
        fclose(log);

        std::string password = menu.getPassword(selected_user);

        log = fopen("/tmp/main-full.log", "a");
        fprintf(log, "[main] Password received (length: %zu)\n", password.length());
        fflush(log);

        // 3. VT allocate
        fprintf(log, "[main] Calling allocateVT\n");
        fflush(log);

        int vt_number = allocateVT();

        fprintf(log, "[main] VT allocated: %d\n", vt_number);
        fflush(log);
        fclose(log);

        menu.showMessage("Authenticating...", false);

        log = fopen("/tmp/main-full.log", "a");

        // 4. Session selection
        fprintf(log, "[main] Getting available sessions\n");
        fflush(log);

        auto sessions = sessionMgr.getAvailableSessions();
        fprintf(log, "[main] Got %zu sessions\n", sessions.size());
        fflush(log);

        std::vector<std::string> session_names;
        for (const auto& s : sessions) {
            session_names.push_back(s.name + " (" + s.type + ")");
            fprintf(log, "[main] Session: %s (type: %s)\n", s.name.c_str(), s.type.c_str());
            fflush(log);
        }
        session_names.push_back("Back");

        fprintf(log, "[main] Calling selectFromList for sessions\n");
        fflush(log);
        fclose(log);

        int session_choice = menu.selectFromList("Select Session", session_names);

        log = fopen("/tmp/main-full.log", "a");
        fprintf(log, "[main] Session choice: %d\n", session_choice);
        fflush(log);

        if (session_choice == -1 || session_names[session_choice] == "Back") {
            fprintf(log, "[main] Back selected, continuing loop\n");
            fclose(log);
            continue;
        }

        fprintf(log, "[main] Selected session: %s\n", sessions[session_choice].name.c_str());
        fprintf(log, "[main] Session type: %s\n", sessions[session_choice].type.c_str());
        fflush(log);

        // 5. PAM SESSION AÇ
        fprintf(log, "[main] Opening PAM session with type: %s\n", sessions[session_choice].type.c_str());
        fflush(log);

        PamSession* pam_session = auth.openSession(
            selected_user,
            password,
            vt_number,
            sessions[session_choice].type
        );

        if (!pam_session) {
            fprintf(log, "[main] PAM authentication failed\n");
            fclose(log);
            menu.showMessage("Authentication failed!", true);
            continue;
        }

        fprintf(log, "[main] PAM session opened successfully\n");
        fprintf(log, "[main] Session ID: %s\n", pam_session->session_id.c_str());
        fprintf(log, "[main] Runtime Dir: %s\n", pam_session->runtime_dir.c_str());
        fflush(log);
        fclose(log);

        menu.showMessage("Starting session...", false);

        log = fopen("/tmp/main-full.log", "a");
        fprintf(log, "[main] Calling startSession\n");
        fflush(log);
        fprintf(log, "[main] User: %s\n", selected_user.c_str());
        fprintf(log, "[main] Session: %s\n", sessions[session_choice].name.c_str());
        fprintf(log, "[main] Session ID: %s\n", pam_session->session_id.c_str());
        fprintf(log, "[main] Runtime Dir: %s\n", pam_session->runtime_dir.c_str());
        fprintf(log, "[main] VT: %d\n", vt_number);
        fflush(log);

        bool session_started = sessionMgr.startSession(
            selected_user,
            sessions[session_choice],
            pam_session->session_id,
            pam_session->runtime_dir,
            vt_number
        );

        fprintf(log, "[main] startSession returned: %d\n", session_started);
        fflush(log);

        if (!session_started) {
            fprintf(log, "[main] Session failed to start\n");
            fclose(log);
            menu.showMessage("Failed to start session!", true);
            auth.closeSession(pam_session);
            continue;
        }

        // 6. Session başarıyla başladı - GREETER KAPANIYOR
        fprintf(log, "[main] Session started successfully\n");
        fprintf(log, "[main] Greeter exiting, session running in background\n");
        fprintf(log, "[main] PAM session remains open for child process\n");
        fflush(log);
        fclose(log);

        // PAM session'ı KAPATMIYORUZ - child process kullanıyor!
        // Greeter kapanıyor, systemd restart edecek
        return 0;
    }

    return 0;
}