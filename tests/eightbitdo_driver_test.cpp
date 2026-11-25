// Exercise the production 8BitDo driver (Ultimate 2.4G in D-input, dock and X-input mode, Ultimate 2
// dongle) with simulated replies; no hardware access.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

namespace {
std::vector<std::vector<BYTE>> pending;
int opens = 0;
int writes = 0;

HidColl Collection(USHORT pid, USHORT page, USHORT usage, USHORT inLen, USHORT outLen) {
    HidColl c;
    c.path = L"8bitdo-test";
    c.vid = 0x2DC8;
    c.pid = pid;
    c.iface = 0;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = inLen;
    c.caps.OutputReportByteLength = outLen;
    return c;
}
HidColl DInput3013() { return Collection(0x3013, 0x0001, 0x0005, 64, 64); }
HidColl Dock3109() { return Collection(0x3109, 0xFFA0, 0x0001, 64, 64); }
HidColl XInput3106() { return Collection(0x3106, 0x0001, 0x0005, 15, 0); }
HidColl Vendor310B() { return Collection(0x310B, 0xFF7A, 0x0001, 64, 64); }
HidColl Vendor6013() { return Collection(0x6013, 0xFF7A, 0x0001, 64, 64); }

// D-input gamepad report, battery in byte 14 (bit 7 = charging).
std::vector<BYTE> DInputReport(BYTE b14) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0x01;
    r[14] = b14;
    return r;
}

// Vendor reply to a status query: 02 04 04 00 <cmd lo> <cmd hi> ..., byte 18 = value.
std::vector<BYTE> VendorReply(BYTE cmdLo, BYTE value) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0x02;
    r[1] = 0x04;
    r[2] = 0x04;
    r[4] = cmdLo;
    r[5] = 0x01;
    r[18] = value;
    return r;
}

// Reply to the device-info query: bytes 22..23 = internal PID (a dongle, not a pad on its cable).
std::vector<BYTE> InfoReply(USHORT internalPid) {
    std::vector<BYTE> r = VendorReply(0x00, 0);
    r[22] = (BYTE)(internalPid & 0xFF);
    r[23] = (BYTE)(internalPid >> 8);
    return r;
}

PollContext Poll(std::vector<HidColl> colls, std::vector<std::vector<BYTE>> replies = {}) {
    pending = std::move(replies);
    opens = 0;
    writes = 0;
    PollContext ctx;
    ctx.colls = std::move(colls);
    Poll8BitDo(ctx);
    return ctx;
}
}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
static void MarkOpen(HidDev& d, const HidColl& c) {
    (void)d;
    (void)c;
    ++opens;
}
bool HidDev::Open(const HidColl& c) {
    c_ = c;
#ifdef _WIN32
    h_ = reinterpret_cast<HANDLE>(1);
#else
    fd_ = 1;
#endif
    MarkOpen(*this, c);
    return true;
}
bool HidDev::OpenReadOnly(const HidColl& c) { return Open(c); }
void HidDev::Close() {
#ifdef _WIN32
    h_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}
void HidDev::Flush() {}
bool HidDev::GetFeature(BYTE, std::vector<BYTE>&) { return false; }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    // Read-only status queries only: 81 04 <00|20|21> 01 ...
    assert(data.size() >= 4 && data[0] == 0x81 && data[1] == 0x04 && data[3] == 0x01);
    assert(data[2] == 0x00 || data[2] == 0x20 || data[2] == 0x21);
    ++writes;
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 1000);
    if (pending.empty()) return -1;
    out = pending.front();
    pending.erase(pending.begin());
    return (int)out.size();
}
std::wstring HidProduct(const HidColl&) { return L"8BitDo test pad"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}
int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int main() {
    const std::wstring key24 = L"8bitdo:ultimate24g";
    const std::wstring hint = L"Switch the controller to D-input mode to see its battery";

    // X-input mode with no level ever read: listed, not asleep, with the hint; never opened or written.
    auto ctx = Poll({ XInput3106() });
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x2DC8, 0x3106));
    {
        const auto& s = ctx.out[0];
        assert(s.key == key24 && s.name == L"8BitDo Ultimate 2.4G" && s.kind == DeviceKind::Gamepad);
        assert(s.via == L"2.4 GHz dongle" && s.connected && s.percent == -1 && !s.asleep && s.note == hint);
        assert(opens == 0 && writes == 0);
    }

    // Docked with a silent vendor channel and no level known yet: today's behaviour, not connected.
    ctx = Poll({ Dock3109() });
    assert(ctx.out.size() == 1 && ctx.out[0].key == key24 && !ctx.out[0].connected && !ctx.out[0].asleep);

    // D-input report, 0x50 = 80% not charging.
    ctx = Poll({ DInput3013() }, { DInputReport(0x50) });
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == key24 && ctx.out[0].percent == 80 && ctx.out[0].connected && !ctx.out[0].charging);
    assert(ctx.out[0].note.empty());

    // Docked, vendor channel silent: listed asleep with the last value (percent -1), same key.
    ctx = Poll({ Dock3109() });
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == key24 && ctx.out[0].connected && ctx.out[0].asleep && ctx.out[0].percent == -1);

    // Docked, vendor channel answers: link query then battery query -> a real level.
    ctx = Poll({ Dock3109() }, { InfoReply(0x3109), VendorReply(0x20, 1), VendorReply(0x21, 0x80 | 55) });
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == key24 && ctx.out[0].percent == 55 && ctx.out[0].connected && !ctx.out[0].asleep);
    assert(ctx.out[0].charging);

    // X-input mode after a known level: asleep, hint, same key, claimed.
    ctx = Poll({ XInput3106() });
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x2DC8, 0x3106));
    assert(ctx.out[0].key == key24 && ctx.out[0].asleep && ctx.out[0].percent == -1 && ctx.out[0].note == hint);
    assert(opens == 0 && writes == 0);

    // A live D-input reading wins over the dock and X-input placeholders listed in the same poll.
    ctx = Poll({ XInput3106(), Dock3109(), DInput3013() }, { DInputReport(0x40) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 64 && !ctx.out[0].asleep && ctx.out[0].note.empty());
    assert(ctx.IsClaimed(0x2DC8, 0x3106) && ctx.IsClaimed(0x2DC8, 0x3109));

    // Ultimate 2 is unchanged: its own key, live vendor reading, then the idle dongle shows asleep.
    ctx = Poll({ Vendor310B() }, { InfoReply(0x6013), VendorReply(0x20, 1), VendorReply(0x21, 60) });
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"8bitdo:ultimate2" && ctx.out[0].percent == 60);
    assert(ctx.out[0].name == L"8BitDo Ultimate 2 Wireless");

    ctx = Poll({ Vendor6013() });
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"8bitdo:ultimate2");
    assert(ctx.out[0].connected && ctx.out[0].asleep && ctx.out[0].percent == -1);

    std::puts("eightbitdo_driver_test: ok");
    return 0;
}
