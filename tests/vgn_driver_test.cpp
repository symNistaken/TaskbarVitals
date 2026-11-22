// Exercise the production VGN "ZHOU" driver with simulated replies; no hardware access.
#include "../src/drivers/drivers.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {

std::vector<std::vector<BYTE>> pending;  // frames the transport answers with, in order
std::vector<BYTE> lastSend;
int opens = 0;
int sends = 0;

// The control collection of the ZHOUTING dongle: 32-byte input, 64-byte output reports.
HidColl Collection(USHORT pid = 0x10BB, USHORT page = 0x000C, USHORT usage = 0x0001, USHORT outLen = 64,
                   USHORT inLen = 32) {
    HidColl c;
    c.path = L"vgn-control";
    c.vid = 0x372E;
    c.pid = pid;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = inLen;
    c.caps.OutputReportByteLength = outLen;
    return c;
}

// What the dongle sent back on real hardware for the read-battery request.
std::vector<BYTE> Reply(BYTE percent) {
    std::vector<BYTE> r(32, 0);
    r[0] = 0x55;  // report id
    r[1] = 0x65;  // read-battery command
    r[2] = percent;
    r[3] = 0x02;
    return r;
}

PollContext Poll(std::vector<std::vector<BYTE>> replies, const HidColl& c = Collection()) {
    pending = std::move(replies);
    sends = 0;
    PollContext ctx;
    ctx.colls = { c };
    PollVgn(ctx);
    return ctx;
}

}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    assert(c.path == L"vgn-control");  // never the FF02 vendor collection or a consumer collection without output
    return true;
}
void HidDev::Close() {}
void HidDev::Flush() {}
int HidDev::SetOutputAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& buf, int timeoutMs) {
    ++sends;
    lastSend = data;
    assert(timeoutMs > 0 && timeoutMs <= 500);
    if (pending.empty()) return -1;
    buf = pending.front();
    pending.erase(pending.begin());
    return (int)buf.size();
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 1000);
    if (pending.empty()) return -1;
    out = pending.front();
    pending.erase(pending.begin());
    return (int)out.size();
}
std::wstring HidProduct(const HidColl&) { return L"VGN ZHOU"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}
int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int main() {
    // Only the read-battery request may be sent, and only as a 0x55 report starting 0x65 0x01.
    auto ctx = Poll({ Reply(90) });
    assert(sends == 1);
    assert(lastSend.size() == 3 && lastSend[0] == 0x55 && lastSend[1] == 0x65 && lastSend[2] == 0x01);

    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x372E, 0x10BB));
    const auto& s = ctx.out[0];
    assert(s.percent == 90 && s.connected && !s.charging && !s.asleep);
    assert(s.name == L"VGN ZHOU" && s.key == L"vgn:zhou");
    assert(s.kind == DeviceKind::Headset && s.via == L"2.4 GHz receiver" && s.graceSec == 1800);

    // An unnumbered reply (0x65 first) is read the same way.
    std::vector<BYTE> unnumbered(32, 0);
    unnumbered[0] = 0x65;
    unnumbered[1] = 75;
    ctx = Poll({ unnumbered });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 75);

    // Full charge and an empty battery are both valid.
    ctx = Poll({ Reply(100) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100);
    ctx = Poll({ Reply(0) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0);

    // A reply with an impossible percentage is not trusted, and unrelated notifications are skipped.
    std::vector<BYTE> volume(32, 0);
    volume[0] = 0x02;
    ctx = Poll({ Reply(101) });
    assert(ctx.out.empty() && ctx.claimed.empty());
    ctx = Poll({ volume, volume });
    assert(ctx.out.empty() && ctx.claimed.empty() && sends == 1);

    // Silence: nothing is reported or claimed.
    ctx = Poll({});
    assert(ctx.out.empty() && ctx.claimed.empty() && sends == 1);

    // The battery frame may arrive behind an unrelated notification.
    ctx = Poll({ volume, Reply(42) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42 && sends == 1);

    // Other products and the wrong collections are never opened.
    int before = opens;
    ctx = Poll({}, Collection(0x10BD));                     // Siren V2: untested, not enabled
    assert(ctx.out.empty() && opens == before);
    ctx = Poll({}, Collection(0x10BB, 0xFF02, 0x0002));      // vendor collection, no output reports accepted there
    assert(ctx.out.empty() && opens == before);
    ctx = Poll({}, Collection(0x10BB, 0x000C, 0x0001, 0, 3));  // media-key collection, no output report
    assert(ctx.out.empty() && opens == before);
    ctx = Poll({}, Collection(0x10BB, 0x000C, 0x0001, 64, 2));  // reply cannot hold the two bytes we read
    assert(ctx.out.empty() && opens == before);
    ctx = Poll({}, Collection(0x10BB, 0x000C, 0x0001, 10, 32));  // output too short for the 64-byte request
    assert(ctx.out.empty() && opens == before);

    puts("VGN driver tests passed");
}
