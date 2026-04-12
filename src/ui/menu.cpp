#include "menu.hpp"

namespace greeter {

Menu::Menu() {
    initscr();
    clear();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    curs_set(0);
    initColors();
}

Menu::~Menu() {
    FILE* log = fopen("/tmp/menu-destructor.log", "w");
    fprintf(log, "Menu destructor called\n");
    fclose(log);
    endwin();
}

void Menu::initColors() {
    start_color();
    init_pair(1, COLOR_CYAN, COLOR_BLACK);    // Title
    init_pair(2, COLOR_GREEN, COLOR_BLACK);   // Selected item
    init_pair(3, COLOR_WHITE, COLOR_BLACK);   // Normal item
    init_pair(4, COLOR_YELLOW, COLOR_BLACK);  // Info/hint text
    init_pair(5, COLOR_RED, COLOR_BLACK);     // Error message
}

int Menu::selectFromList(const std::string& title, const std::vector<std::string>& items) {
    int choice = 0;
    int key;

    while (true) {
        clear();

        int center_x = COLS / 2;
        int start_y = LINES / 2 - items.size() - 3;

        // Title
        attron(COLOR_PAIR(1) | A_BOLD);
        int title_len = title.length();
        mvprintw(start_y, center_x - title_len/2, "%s", title.c_str());
        attroff(COLOR_PAIR(1) | A_BOLD);

        // Divider under the title
        attron(COLOR_PAIR(1));
        for (int i = 0; i < title_len; i++) {
            mvprintw(start_y + 1, center_x - title_len/2 + i, "-");
        }
        attroff(COLOR_PAIR(1));

        // Menu items
        for (size_t i = 0; i < items.size(); i++) {
            // Render separator rows
            if (items[i] == "---") {
                attron(COLOR_PAIR(1));
                for (int j = 0; j < 30; j++) {
                    mvprintw(start_y + 3 + i, center_x - 15 + j, "-");
                }
                attroff(COLOR_PAIR(1));
                continue;
            }

            if (i == choice) {
                // Skip over separator entries when highlighted
                if (items[choice] == "---") {
                    choice++;
                    if (choice >= items.size()) choice = 0;
                    i = choice;
                }

                attron(COLOR_PAIR(2) | A_BOLD | A_REVERSE);
                mvprintw(start_y + 3 + i, center_x - 15, " > %-26s ", items[i].c_str());
                attroff(COLOR_PAIR(2) | A_BOLD | A_REVERSE);
            } else {
                attron(COLOR_PAIR(3));
                mvprintw(start_y + 3 + i, center_x - 15, "   %-26s", items[i].c_str());
                attroff(COLOR_PAIR(3));
            }
        }

        // Footer hint
        attron(COLOR_PAIR(4));
        mvprintw(LINES - 2, 2, "UP/DOWN: Navigate | Enter: Select | Q: Quit");
        attroff(COLOR_PAIR(4));

        refresh();

        key = getch();

        switch (key) {
            case KEY_UP:
                do {
                    choice--;
                    if (choice < 0) choice = items.size() - 1;
                } while (items[choice] == "---");
                break;
            case KEY_DOWN:
                do {
                    choice++;
                    if (choice >= items.size()) choice = 0;
                } while (items[choice] == "---");
                break;
            case 10:  // Enter
                return choice;
            case 'q':
            case 'Q':
                return -1;
        }
    }
}

std::string Menu::getPassword(const std::string& username) {
    clear();

    int center_x = COLS / 2;
    int center_y = LINES / 2;

    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(center_y - 2, center_x - 15, "Login as: %s", username.c_str());
    attroff(COLOR_PAIR(1) | A_BOLD);

    attron(COLOR_PAIR(3));
    mvprintw(center_y, center_x - 15, "Password: ");
    attroff(COLOR_PAIR(3));

    noecho();
    char password[256];
    int pos = 0;
    int ch;

    while ((ch = getch()) != 10 && pos < 255) {
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (pos > 0) {
                pos--;
                mvprintw(center_y, center_x - 15 + 10 + pos, " ");
            }
        } else if (ch >= 32 && ch < 127) {
            password[pos++] = ch;
            mvprintw(center_y, center_x - 15 + 10 + pos - 1, "*");
        }
        refresh();
    }
    password[pos] = '\0';

    return std::string(password);
}

void Menu::showMessage(const std::string& message, bool is_error) {
    clear();

    int center_x = COLS / 2;
    int center_y = LINES / 2;

    if (is_error) {
        attron(COLOR_PAIR(5) | A_BOLD);
    } else {
        attron(COLOR_PAIR(2) | A_BOLD);
    }

    mvprintw(center_y, center_x - message.length()/2, "%s", message.c_str());

    if (is_error) {
        attroff(COLOR_PAIR(5) | A_BOLD);
    } else {
        attroff(COLOR_PAIR(2) | A_BOLD);
    }

    refresh();
    napms(1500);
}

} // namespace greeter
