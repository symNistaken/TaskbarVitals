// Unit tests for Linux taskbar-vitals config/autostart paths, migration resolution, and LF metadata.
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "../src/linux_paths.h"

namespace {

std::string ReadBinaryFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    assert(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    using namespace linuxpaths;

    // Path formatting & trailing-slash normalization
    assert(TrimTrailingSlashes("/home/u/.config/") == "/home/u/.config");
    assert(TrimTrailingSlashes("/") == "/");
    assert(NewLinuxConfigPath("/home/u/.config/") == "/home/u/.config/taskbar-vitals/config.ini");
    assert(LegacyLinuxConfigPath("/home/u/.config") == "/home/u/.config/wireless-battery/config.ini");
    assert(NewLinuxAutostartPath("/home/u/.config/") == "/home/u/.config/autostart/taskbar-vitals.desktop");
    assert(LegacyLinuxAutostartPath("/home/u/.config") == "/home/u/.config/autostart/wireless-battery.desktop");

    // Legacy -> new config migration resolution
    assert(ResolveLinuxConfigReadPath("/cfg", false, false) == "/cfg/taskbar-vitals/config.ini");
    assert(ResolveLinuxConfigReadPath("/cfg", false, true) == "/cfg/wireless-battery/config.ini");
    assert(ResolveLinuxConfigReadPath("/cfg", true, false) == "/cfg/taskbar-vitals/config.ini");
    assert(ResolveLinuxConfigReadPath("/cfg", true, true) == "/cfg/taskbar-vitals/config.ini");

    // Desktop Exec quoting & generated autostart entry
    assert(FormatDesktopExecArg("/usr/local/bin/taskbar-vitals") == "\"/usr/local/bin/taskbar-vitals\"");
    assert(FormatDesktopExecArg("/opt/My App/bin$1") == "\"/opt/My App/bin\\$1\"");
    const std::string entry = FormatLinuxAutostartEntry("/usr/bin/taskbar-vitals");
    assert(entry.find("Name=Taskbar Vitals\n") != std::string::npos);
    assert(entry.find("Exec=\"/usr/bin/taskbar-vitals\"\n") != std::string::npos);
    assert(entry.find("Icon=taskbar-vitals\n") != std::string::npos);
    assert(entry.find('\r') == std::string::npos);
    assert(entry.find("wireless-battery") == std::string::npos);

    // Verify committed Linux packaging metadata uses taskbar-vitals and keeps LF endings
    const std::string desktop = ReadBinaryFile("linux/taskbar-vitals.desktop");
    assert(desktop.find("Name=Taskbar Vitals") != std::string::npos);
    assert(desktop.find("Exec=taskbar-vitals") != std::string::npos);
    assert(desktop.find("Icon=taskbar-vitals") != std::string::npos);
    assert(desktop.find("wireless-battery") == std::string::npos);
    assert(desktop.find('\r') == std::string::npos);

    const std::string mk = ReadBinaryFile("linux/Makefile.linux");
    assert(mk.find("TARGET := taskbar-vitals") != std::string::npos);
    assert(mk.find("wireless-battery") == std::string::npos);
    assert(mk.find('\r') == std::string::npos);

    const std::string sh = ReadBinaryFile("build-linux.sh");
    assert(sh.find("taskbar-vitals") != std::string::npos);
    assert(sh.find("wireless-battery") == std::string::npos);
    assert(sh.find('\r') == std::string::npos);

    const std::string rules = ReadBinaryFile("linux/70-taskbar-vitals.rules");
    assert(rules.find("SUBSYSTEM==\"hidraw\"") != std::string::npos);
    assert(rules.find('\r') == std::string::npos);

    const std::string mainLinux = ReadBinaryFile("src/linux/main_linux.cpp");
    assert(mainLinux.find("taskbar-vitals") != std::string::npos);
    assert(mainLinux.find("#include \"../linux_paths.h\"") != std::string::npos);
    assert(mainLinux.find("wireless-battery") == std::string::npos);
    assert(mainLinux.find('\r') == std::string::npos);

    std::puts("Linux paths, migration, and metadata LF tests passed.");
    return 0;
}
