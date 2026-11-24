#pragma once
// Pure path and XDG desktop/autostart helpers for the Linux tray build.
// Header-only and free of GTK/GIO types so Linux and Windows unit tests can share it directly.
#include <string>

namespace linuxpaths {

inline std::string TrimTrailingSlashes(const std::string& dir) {
    size_t end = dir.size();
    while (end > 1 && dir[end - 1] == '/') --end;
    return dir.substr(0, end);
}

inline std::string NewLinuxConfigPath(const std::string& xdgConfigDir) {
    return TrimTrailingSlashes(xdgConfigDir) + "/taskbar-vitals/config.ini";
}

inline std::string LegacyLinuxConfigPath(const std::string& xdgConfigDir) {
    return TrimTrailingSlashes(xdgConfigDir) + "/wireless-battery/config.ini";
}

inline std::string ResolveLinuxConfigReadPath(const std::string& xdgConfigDir,
                                              bool newExists,
                                              bool legacyExists) {
    if (newExists) return NewLinuxConfigPath(xdgConfigDir);
    if (legacyExists) return LegacyLinuxConfigPath(xdgConfigDir);
    return NewLinuxConfigPath(xdgConfigDir);
}

inline std::string NewLinuxAutostartPath(const std::string& xdgConfigDir) {
    return TrimTrailingSlashes(xdgConfigDir) + "/autostart/taskbar-vitals.desktop";
}

inline std::string LegacyLinuxAutostartPath(const std::string& xdgConfigDir) {
    return TrimTrailingSlashes(xdgConfigDir) + "/autostart/wireless-battery.desktop";
}

inline std::string FormatDesktopExecArg(const std::string& exePath) {
    std::string out = "\"";
    for (char ch : exePath) {
        if (ch == '"' || ch == '\\' || ch == '$' || ch == '`') out.push_back('\\');
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

inline std::string FormatLinuxAutostartEntry(const std::string& exePath) {
    return "[Desktop Entry]\nType=Application\nName=Taskbar Vitals\n"
           "Comment=Show battery levels of wireless devices in the tray\n"
           "Exec=" + FormatDesktopExecArg(exePath) +
           "\nIcon=taskbar-vitals\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\n";
}

}  // namespace linuxpaths
