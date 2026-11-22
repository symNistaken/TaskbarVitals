// Exercise the production MCHOSE first-generation headset driver (291D:385D dongle, "BLK" protocol)
// with simulated replies; no hardware access.
#include "../src/drivers/drivers.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
std::vector<std::vector<BYTE>> pending;
int opens = 0;
int writes = 0;

HidColl Collection(USHORT page = 0xFF90, USHORT usage = 0x0001, USHORT outLen = 65, USHORT inLen = 65) {
    HidColl c;
    c.path = L"mchose-v9pro";
    c.vid = 0x291D;
    c.pid = 0x385D;
    c.iface = 0;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = inLen;
    c.caps.OutputReportByteLength = outLen;
    return c;
}

// Battery reply laid out the way the dongle sends it: report id, command, level, state.
std::vector<BYTE> Reply(BYTE level, BYTE state) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0x55;
    r[1] = 0x65;
    r[2] = level;
    r[3] = state;
    return r;
}

PollContext Poll(std::vector<std::vector<BYTE>> replies, const HidColl& c = Collection()) {
    pending = std::move(replies);
    writes = 0;
    PollContext ctx;
    ctx.colls = { c };
    PollMchoseV9Pro(ctx);
    return ctx;
}
}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
bool HidDev::Open(const HidColl& c) {
    c_ = c;
#ifdef _WIN32
    h_ = reinterpret_cast<HANDLE>(1);
#else
    fd_ = 1;
#endif
    ++opens;
    return true;
}
void HidDev::Close() {
#ifdef _WIN32
    h_ = INVALID_HANDLE_VALUE;
#else
    fd_ = -1;
#endif
}
// The scripted replies stand for what the dongle will answer next, so there is nothing stale to drop.
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    // The only frame this driver may send: the read-only BLK battery query, zero padded to the
    // 64-byte report (the transport pads; the driver passes the three meaningful bytes).
    const std::vector<BYTE> expected = { 0x55, 0x65, 0x01 };
    assert(data.size() == expected.size());
    assert(std::equal(expected.begin(), expected.end(), data.begin()));
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
std::wstring HidProduct(const HidColl&) { return L"MCHOSE V9 PRO"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}
int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int main() {
    // Discharging: what a real V9 PRO dongle answered (55 65 28 02 = 40%).
    auto ctx = Poll({ Reply(40, 2) });
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x291D, 0x385D));
    {
        const auto& s = ctx.out[0];
        assert(s.percent == 40 && s.connected && !s.charging && !s.full && !s.asleep);
        assert(s.name == L"MCHOSE V9 PRO" && s.key == L"mchose:v9pro1");
        assert(s.kind == DeviceKind::Headset && s.via == L"2.4 GHz receiver" && s.graceSec == 1800);
        assert(writes == 1);  // one reply is enough, no retries
    }

    // Charging.
    ctx = Poll({ Reply(55, 3) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 55 && ctx.out[0].charging && !ctx.out[0].full);

    // Fully charged.
    ctx = Poll({ Reply(100, 4) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].full && !ctx.out[0].charging);

    // Headset switched off or asleep: the dongle still answers, so the entry stays but keeps its
    // last value instead of showing a bogus 0%.
    ctx = Poll({ Reply(0, 26) });
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].percent == -1);

    // Unrelated traffic on the same collection is ignored, the query is retried within the deadline.
    ctx = Poll({ std::vector<BYTE>(64, 0), Reply(12, 2) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 12 && writes == 1);

    // Nothing answers (headset off, receiver idle): nothing is reported and the id stays unclaimed.
    ctx = Poll({});
    assert(ctx.out.empty() && !ctx.IsClaimed(0x291D, 0x385D));

    // A different collection layout on the same id is not this protocol (e.g. the FF21 interface).
    ctx = Poll({ Reply(40, 2) }, Collection(0xFF21, 0x0001));
    assert(ctx.out.empty() && !ctx.IsClaimed(0x291D, 0x385D));

    // A level above 100 is rejected as a malformed frame.
    ctx = Poll({ Reply(120, 2) });
    assert(ctx.out.empty());

    puts("MCHOSE V9 PRO driver tests passed");
    return 0;
}
