// Portable tests for IgnoreDevices parsing/formatting and PollContext::Add PID population.
#include "../src/ignore_devices.h"
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace ignore_devices;

    // Empty and separator-only inputs.
    assert(Parse(L"").empty());
    assert(Parse(L"   , ; \t \r\n ").empty());

    // Separators: commas, semicolons, spaces, tabs, newlines, mixed case, 1..4 hex digits.
    const auto parsed = Parse(L"28bd:0947, 046D:C547;1532:0084\t1B1C:1B8F\n46d:a");
    assert(parsed.size() == 5);
    assert((parsed[0] == VidPid{ 0x28BD, 0x0947 }));
    assert((parsed[1] == VidPid{ 0x046D, 0xC547 }));
    assert((parsed[2] == VidPid{ 0x1532, 0x0084 }));
    assert((parsed[3] == VidPid{ 0x1B1C, 0x1B8F }));
    assert((parsed[4] == VidPid{ 0x046D, 0x000A }));

    // 0x / 0X prefixes and duplicate/invalid token filtering preserving order.
    const auto filtered = Parse(
        L"0x28BD:0x0947, badtoken, 12345:6789, 1234:56789, :1234, 1234:, 12:34:56, "
        L"12G4:5678, 1234:56Z8, 0x:1234, 1234:0x12345, -1:1234, 0000:0000, "
        L"0X046d:0XC547, 28bd:0947, 1532:0084, 0x046D:C547");
    assert(filtered.size() == 3);
    assert((filtered[0] == VidPid{ 0x28BD, 0x0947 }));
    assert((filtered[1] == VidPid{ 0x046D, 0xC547 }));
    assert((filtered[2] == VidPid{ 0x1532, 0x0084 }));

    assert(Contains(filtered, 0x046D, 0xC547));
    assert(!Contains(filtered, 0x046D, 0xC548));

    // Formatting.
    assert(FormatPair(0x046D, 0x0042) == L"046D:0042");
    assert(FormatPair(0x0001, 0xABCD) == L"0001:ABCD");
    assert(FormatList({}).empty());
    assert(FormatList(filtered) == L"28BD:0947, 046D:C547, 1532:0084");

    // Add / Remove roundtrips preserving manual entries.
    assert(Add(L"", 0x046D, 0xC547) == L"046D:C547");
    assert(Add(L"28bd:0947; 46d:c547", 0x1532, 0x0084) == L"28BD:0947, 046D:C547, 1532:0084");
    assert(Add(L"28bd:0947; 046D:C547", 0x046D, 0xC547) == L"28BD:0947, 046D:C547");
    assert(Add(L"28bd:0947", 0, 0x1234) == L"28BD:0947");
    assert(Add(L"28bd:0947", 0x1234, 0) == L"28BD:0947");
    assert(Add(L"", 0, 0).empty());

    std::wstring list = L"28bd:0947; 046D:C547, 1532:0084";
    list = Remove(list, 0x046D, 0xC547);
    assert(list == L"28BD:0947, 1532:0084");
    list = Remove(list, 0x9999, 0x1111);
    assert(list == L"28BD:0947, 1532:0084");
    list = Remove(list, 0x28BD, 0x0947);
    assert(list == L"1532:0084");
    list = Remove(list, 0x1532, 0x0084);
    assert(list.empty());

    // PollContext::Add populates DeviceStatus::vid and DeviceStatus::pid without overwriting preset non-zero ids.
    PollContext ctx;
    DeviceStatus s;
    assert(s.vid == 0 && s.pid == 0);
    s.key = L"test:mouse";
    ctx.Add(s, 0x046D, 0xC547);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].vid == 0x046D && ctx.out[0].pid == 0xC547);
    assert(ctx.IsClaimed(0x046D, 0xC547));

    DeviceStatus preset;
    preset.key = L"test:preset";
    preset.vid = 0x1532;
    preset.pid = 0x0084;
    ctx.Add(preset, 0x1532, 0x0085);
    assert(ctx.out.size() == 2);
    assert(ctx.out[1].vid == 0x1532 && ctx.out[1].pid == 0x0084);
    assert(ctx.IsClaimed(0x1532, 0x0085));

    DeviceStatus bleOnly;
    bleOnly.key = L"gamesir-ble:addr";
    ctx.Add(bleOnly, 0x3537, 0);
    assert(ctx.out.size() == 3);
    assert(ctx.out[2].vid == 0x3537 && ctx.out[2].pid == 0);

    std::puts("Ignore devices tests passed");
    return 0;
}
