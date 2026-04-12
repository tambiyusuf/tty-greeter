#pragma once

#include <ncurses.h>
#include <vector>
#include <string>

namespace greeter {

class Menu {
public:
    Menu();
    ~Menu();

    // Displays a list and returns the index of the selected item (-1 if cancelled)
    int selectFromList(const std::string& title, const std::vector<std::string>& items);

    // Prompts for a password with masked input
    std::string getPassword(const std::string& username);

    // Displays a status message; pass is_error=true to render it in red
    void showMessage(const std::string& message, bool is_error = false);

private:
    void initColors();
};

} // namespace greeter
