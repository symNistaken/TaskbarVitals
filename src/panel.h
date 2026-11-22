#pragma once
// Settings panel: a small window that replaces the long tray menu. Sections in tabs (Devices, System,
// Appearance, General), switches for on/off options, a value with a drop-down for choices, and one row per
// device that opens the device's own menu.
//
// The panel has no settings logic of its own. Its model is the app's menu (main.cpp's BuildMenu): it reads
// labels, checked and greyed states and submenus with GetMenuItemInfo, and runs a click through the menu's
// command. So every menu option shows up in the panel, with the menu's translated labels, and runs exactly the
// code the menu runs. The menu is rebuilt for every click, so commands that are list positions (devices,
// monitors, languages) always match the lists of that moment.
#include <windows.h>

#include <string>
#include <vector>

namespace panel {

enum class Section { Skip, Devices, System, Appearance, General, Footer };
enum class Kind { Device, Toggle, Radio, Choice, Action };
struct Place {
    Section section = Section::Skip;
    Kind kind = Kind::Action;
};

// One device row, in the order of the device submenus in the menu.
struct Device {
    std::wstring key, name, status;
    wchar_t glyph = 0;
    bool hidden = false;
};

struct Host {
    // Builds the menu with the current state and fills the device rows. The menu stays valid until release().
    HMENU (*build)(std::vector<Device>& devices) = nullptr;
    // Runs a command of the menu built last.
    void (*run)(UINT command) = nullptr;
    // Destroys the menu built last.
    void (*release)() = nullptr;
    // Where a menu item goes: by its command id, or by the first command id of a submenu.
    Place (*place)(UINT id) = nullptr;
    // Light or dark, as the menus (the taskbar theme).
    bool (*light)() = nullptr;
};

// Opens the panel near the taskbar of the monitor under the cursor, or brings it to the front.
void Show(HINSTANCE inst, const Host& host);
// Reads the state again (new poll results, theme or language change). Does nothing while the panel is closed.
void Refresh();
// Closes the panel (the app is exiting).
void Close();

}  // namespace panel
