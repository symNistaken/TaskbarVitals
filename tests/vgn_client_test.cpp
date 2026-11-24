// Exercise the actual VGN driver with a simulated HID transport: no USB writes and no hardware.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <map>

namespace {
std::map<std::wstring, std::vector<std::vector<BYTE>>> reports;
int opens = 0;

// One collection of a VGN device. `feature` selects the channel: the wired keyboards answer
// feature reports, everything else uses an output report.
HidColl Collection(USHORT vid, USHORT pid, const wchar_t* path, USHORT page, USHORT usage, USHORT len, bool feature) {
    HidColl c;
    c.path = path;
    c.vid = vid;
    c.pid = pid;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    if (feature) {
        c.caps.FeatureReportByteLength = len;
        c.caps.InputReportByteLength = 0;
        c.caps.OutputReportByteLength = 0;
    } else {
        c.caps.OutputReportByteLength = len;
        c.caps.InputReportByteLength = len;
    }
    return c;
}
}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
bool HidDev::Open(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    ++opens;
    return true;
}
bool HidDev::OpenFeatureOnly(const HidColl& c) { return Open(c); }
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
bool HidDev::Write(const std::vector<BYTE>&, int) { return true; }
void HidDev::Flush() {}
bool HidDev::SetFeature(const std::vector<BYTE>&) { return true; }
bool HidDev::GetFeature(BYTE, std::vector<BYTE>& out) {
    auto& queue = reports[c_.path];
    if (queue.empty()) return false;
    out = queue.front();
    queue.erase(queue.begin());
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 4000);
    auto& queue = reports[c_.path];
    if (queue.empty()) return -1;
    out = queue.front();
    queue.erase(queue.begin());
    return (int)out.size();
}
std::wstring HidProduct(const HidColl&) { return L"VGN Test Device"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    // ---- Y2 Ultra: the online probe goes unanswered, the retry returns the level -------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0x3554, 0xFB3D, L"y2", 0xFF02, 2, 16, false) };
        // [id][cmd][0][addrHi][addrLo][len][level][charging][mvHi][mvLo]
        std::vector<BYTE> level(16, 0);
        level[0] = 0x08;   // report id
        level[1] = 0x04;   // BatteryLevel
        level[6] = 92;     // level   (payload byte 5 once the id is stripped)
        level[7] = 0;      // charging
        level[8] = 0x0F;   // voltage hi
        level[9] = 0x8C;   // voltage lo
        reports = { { L"y2", { level } } };
        PollVgnClient(ctx);
        assert(ctx.out.size() == 1);
        assert(ctx.out[0].percent == 92 && !ctx.out[0].charging);
        assert(ctx.out[0].millivolts == 0x0F8C);
        assert(ctx.out[0].kind == DeviceKind::Mouse);
        assert(ctx.IsClaimed(0x3554, 0xFB3D));
    }

    // ---- No answer at all: the device is neither claimed nor reported as 0% ------------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0x3554, 0xFB3D, L"silent", 0xFF02, 2, 16, false) };
        reports.clear();
        PollVgnClient(ctx);
        assert(ctx.out.empty() && ctx.claimed.empty());
    }

    // ---- S99 dongle: the right collection is used even when the keyboard one comes first -----
    {
        PollContext ctx;
        ctx.colls = { Collection(0x320F, 0x5088, L"kbd-collection", 0x0001, 6, 64, false),
                      Collection(0x320F, 0x5088, L"vendor", 0xFF1C, 146, 64, false) };
        std::vector<BYTE> reply(64, 0);
        reply[0] = 0x04;  // report id
        reply[3] = 0x1A;  // GetBatterLevel echoes at +2
        reply[4] = 0x06;
        reply[8] = 74;    // level
        reports = { { L"vendor", { reply } } };
        PollVgnClient(ctx);
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 74);
        assert(ctx.out[0].via == L"2.4 GHz receiver");
        assert(ctx.out[0].kind == DeviceKind::Keyboard);
    }

    // ---- A sleeping S99 answers 0xFF: not reported, not claimed -----------------------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0x320F, 0x5088, L"asleep", 0xFF1C, 146, 64, false) };
        std::vector<BYTE> asleep = { 0x04, 0x00, 0x00, 0xFF, 0x06, 0x00, 0x00, 0x00, 0x00 };
        asleep.resize(64, 0);
        reports = { { L"asleep", { asleep } } };
        PollVgnClient(ctx);
        assert(ctx.out.empty() && ctx.claimed.empty());
    }

    // ---- N87 over its 240 report -----------------------------------------------------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0xA8A4, 0x2355, L"n87", 0xFF01, 16, 64, false) };
        std::vector<BYTE> reply(64, 0);
        reply[0] = 0xF0;  // report id 240
        reply[1] = 0x30;  // GetBattery
        reply[2] = 0x02;
        reply[4] = 0x02;
        reply[8] = 55;
        reply[9] = 0x01;  // charging
        reports = { { L"n87", { reply } } };
        PollVgnClient(ctx);
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 55 && ctx.out[0].charging);
    }

    // ---- V98Pro V3 wired keyboard answers the feature channel ------------------------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0x258A, 0x024E, L"v98", 0xFF00, 1, 520, true) };
        std::vector<BYTE> reply(520, 0);
        reply[0] = 0x06;  // report id
        reply[9] = 39;    // level at +8 plus the id
        reports = { { L"v98", { reply, reply, reply } } };
        PollVgnClient(ctx);
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 39);
        assert(ctx.out[0].via == L"USB");
    }

    // ---- Unlisted product id of a known vendor: ignored --------------------------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0x3554, 0xFFFF, L"unknown", 0xFF02, 2, 16, false) };
        reports.clear();
        opens = 0;
        PollVgnClient(ctx);
        assert(opens == 0 && ctx.out.empty());
    }

    // ---- Already claimed devices are skipped -------------------------------------------
    {
        PollContext ctx;
        ctx.colls = { Collection(0x3554, 0xFB3D, L"claimed", 0xFF02, 2, 16, false) };
        ctx.Claim(0x3554, 0xFB3D);
        opens = 0;
        PollVgnClient(ctx);
        assert(opens == 0 && ctx.out.empty());
    }

    std::puts("VGN driver tests passed (collection selection, retry, sleep handling, dedup).");
    return 0;
}
