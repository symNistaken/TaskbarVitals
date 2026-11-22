// Tests for the shared --json formatter (no hardware needed).
// Run with: make -f linux/Makefile.linux test
#include "../src/json_output.h"

#include <cassert>
#include <string>
#include <vector>

int main() {
    assert(DevicesToJson({}) == "[]\n");

    DeviceStatus s;
    s.key = L"kysona:373B:1085";
    s.name = L"He said \"hi\" and left";  // quotes must be escaped
    s.via = L"2.4 GHz receiver";
    s.kind = DeviceKind::Headset;
    s.percent = 42;
    s.levelText = L"";
    s.detail = L"L 44%, R 40%";
    s.charging = true;
    s.asleep = false;
    s.connected = true;
    s.millivolts = 0;
    s.graceSec = 900;

    const std::string json = DevicesToJson({s});
    assert(json.front() == '[' && json.back() == '\n');
    assert(json.find("\"kind\":\"headset\"") != std::string::npos);
    assert(json.find("\"percent\":42") != std::string::npos);
    assert(json.find("\"charging\":true") != std::string::npos);
    assert(json.find("\"asleep\":false") != std::string::npos);
    assert(json.find("\"connected\":true") != std::string::npos);
    assert(json.find("\"graceSec\":900") != std::string::npos);
    assert(json.find("He said \\\"hi\\\" and left") != std::string::npos);

    // Backslashes and control characters are escaped too.
    DeviceStatus weird = s;
    weird.name = L"back\\slash\nnext";
    const std::string escaped = DevicesToJson({weird});
    assert(escaped.find("back\\\\slash") != std::string::npos);
    assert(escaped.find("\\nnext") != std::string::npos);

    // One object per device, separated by commas.
    DeviceStatus mouse = s;
    mouse.kind = DeviceKind::Mouse;
    mouse.name = L"Mouse";
    const std::string two = DevicesToJson({s, mouse});
    assert(two.find("\"kind\":\"headset\"") != std::string::npos);
    assert(two.find("\"kind\":\"mouse\"") != std::string::npos);
    assert(two.find("},{") != std::string::npos);

    return 0;
}
