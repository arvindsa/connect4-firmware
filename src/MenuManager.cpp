#include "MenuManager.h"

MenuManager::MenuManager(Adafruit_SSD1306* disp) {
    display = disp;
    indexStack.push_back(0);
    firstVisibleIndex = 0;  // Initialize firstVisibleIndex
}

void MenuManager::setRootMenu(const std::vector<MenuItem>& menu) {
    currentMenu = menu;
    menuStack.clear();
    indexStack.clear();
    indexStack.push_back(0);
    firstVisibleIndex = 0;  // Reset the visible index when setting a new root menu
}

void MenuManager::navigateUp() {
    if (indexStack.back() > 0) {
        indexStack.back()--;
        updateVisibleWindow();  // Adjust the visible window when focus changes
    }
}

void MenuManager::navigateDown() {
    if (indexStack.back() < currentMenu.size() - 1) {
        indexStack.back()++;
        updateVisibleWindow();  // Adjust the visible window when focus changes
    }
}

void MenuManager::enterSubMenu(const std::vector<MenuItem>& subMenu) {
    menuStack.push_back(currentMenu);
    currentMenu = subMenu;
    currentMenu.insert(currentMenu.begin(), {"Back", nullptr, {}}); // add Back at top
    indexStack.push_back(0);
    firstVisibleIndex = 0;  // Reset the visible index when entering a submenu
}

void MenuManager::goBack() {
    if (!menuStack.empty()) {
        currentMenu = menuStack.back();
        menuStack.pop_back();
        indexStack.pop_back();

        // After going back, adjust firstVisibleIndex so the focused item is in view
        int focus = indexStack.back();
        const int visibleLines = 4;
        if (focus < firstVisibleIndex) {
            firstVisibleIndex = focus;  // Ensure that focus is visible
        } else if (focus >= firstVisibleIndex + visibleLines) {
            firstVisibleIndex = focus - visibleLines + 1;  // Adjust the visible window
        }
    }
}

void MenuManager::select() {
    int idx = indexStack.back();
    if (idx >= currentMenu.size()) return;

    MenuItem selected = currentMenu[idx];

    if (idx == 0 && !menuStack.empty()) {  // < Back selected
        goBack();
    }
    else if (!selected.subMenu.empty()) {
        enterSubMenu(selected.subMenu);
    }
    else if (selected.action) {
        selected.action();
        // move setting mode OUTSIDE so action can set it if needed
    }
}

void MenuManager::updateVisibleWindow() {
    const int visibleLines = 4;  // The number of lines visible on the display
    int focus = indexStack.back();

    // If the focus moves outside the visible window, adjust the first visible index
    if (focus < firstVisibleIndex) {
        firstVisibleIndex = max(0, focus);  // Ensure we don't go below 0
    } else if (focus >= firstVisibleIndex + visibleLines) {
        firstVisibleIndex = focus - visibleLines + 1;
    }
}

void MenuManager::render() {
    if (!display) return;

    display->clearDisplay();
    display->setTextSize(2);
    display->setTextColor(SSD1306_WHITE);

    const int visibleLines = 4;
    int lastVisible = min(firstVisibleIndex + visibleLines, (int)currentMenu.size());

    for (int i = firstVisibleIndex; i < lastVisible; i++) {
        display->setCursor(0, (i - firstVisibleIndex) * 16);

        String prefix = (i == indexStack.back()) ? ">" : "";
        String name = currentMenu[i].name.c_str();

        int maxNameLength = 10 - prefix.length();
        if (name.length() > maxNameLength) {
            name = name.substring(0, maxNameLength);
        }

        display->print(prefix + name);
        display->println();
    }

    display->display();
}
