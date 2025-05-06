#ifndef MENUMANAGER_H
#define MENUMANAGER_H

#include <vector>
#include <Adafruit_SSD1306.h>

struct MenuItem {
    String name;
    void (*action)();
    std::vector<MenuItem> subMenu;
};

class MenuManager {
public:
    MenuManager(Adafruit_SSD1306* disp);

    void setRootMenu(const std::vector<MenuItem>& menu);
    void navigateUp();
    void navigateDown();
    void enterSubMenu(const std::vector<MenuItem>& subMenu);
    void goBack();
    void select();
    void render();

private:
    Adafruit_SSD1306* display;
    std::vector<MenuItem> currentMenu;
    std::vector<std::vector<MenuItem>> menuStack;
    std::vector<int> indexStack;

    // Declare firstVisibleIndex as a member variable
    int firstVisibleIndex;

    void updateVisibleWindow();
};

#endif // MENUMANAGER_H
