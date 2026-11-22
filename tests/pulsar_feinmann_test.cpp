// Exercise the real Pulsar battery poller with simulated HID feature reports.
// Does not touch physical hardware or send real device commands.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <deque>

namespace {
std::deque<std::vector<BYTE>> responses;
int opens = 0, requests = 0, reads = 0;
bool openOk = true, writeOk = true;

HidColl Coll(USHORT pid = 0x5404, int iface = 3) {
    HidColl c;
    c.vid = 0x3710;
    c.pid = pid;
    c.iface = iface;
    c.caps.UsagePage = 0xFFFF;
    c.caps.Usage = 0x0001;
    c.caps.InputReportByteLength = 65;
    c.caps.OutputReportByteLength = 65;
    c.caps.FeatureReportByteLength = 65;
    return c;
}

// Windows GetFeature places the report ID in byte 0; the 64-byte device reply follows.
std::vector<BYTE> Answer(BYTE status, BYTE percent, BYTE category = 0x08,
                         BYTE reg = 0x81, BYTE sub = 0x01) {
    std::vector<BYTE> r(65, 0);
    r[1] = status;
    r[2] = category;
    r[3] = reg;
    r[4] = sub;
    r[7] = percent;  // payload byte 6
    return r;
}

PollContext Poll(const HidColl& c, std::deque<std::vector<BYTE>> replies) {
    opens = requests = reads = 0;
    responses = std::move(replies);
    PollContext ctx;
    ctx.colls = { c };
    PollPulsar(ctx);
    return ctx;
}
} // namespace

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    ++opens;
    if (!openOk) return false;
    c_ = c;
    return true;
}
void HidDev::Close() {}
bool HidDev::SetFeature(const std::vector<BYTE>& q) {
    ++requests;
    assert(q.size() == 65 && q[0] == 0);
    assert(q[1] == 0x00 && q[2] == 0x08 && q[3] == 0x81 && q[4] == 0x01);
    for (size_t i = 5; i < 63; ++i) assert(q[i] == 0);
    assert(q[63] == 0x8A && q[64] == 0);
    return writeOk;
}
bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    ++reads;
    assert(id == 0x00);
    if (responses.empty()) return false;
    out = responses.front();
    responses.pop_front();
    return true;
}
std::wstring HidProduct(const HidColl&) { return L"Pulsar 8K Dongle"; }
std::wstring Hex4(USHORT id) {
    wchar_t value[8];
    swprintf(value, 8, L"%04X", id);
    return value;
}

int main() {
    // Correct receiver: a pending feature reply followed by a valid battery level.
    auto ctx = Poll(Coll(), { Answer(0x02, 0), Answer(0x01, 76) });
    assert(opens == 1 && requests == 1 && reads == 2);
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x3710, 0x5404));
    const auto& s = ctx.out.front();
    assert(s.name == L"Pulsar Feinmann FO1" && s.key == L"pulsar:5404");
    assert(s.kind == DeviceKind::Mouse && s.via == L"2.4 GHz receiver");
    assert(s.connected && s.percent == 76 && !s.charging && s.vid == 0x3710);
    PollPulsar(ctx);
    assert(opens == 1 && requests == 1);  // already claimed in this poll

    // 0% from the receiver is treated as disconnected, not a confirmed empty cell.
    ctx = Poll(Coll(), { Answer(0x01, 0) });
    assert(ctx.out.size() == 1 && !ctx.out[0].connected && ctx.out[0].percent == -1);

    // A bogus percentage must not be shown as 100% or accepted as a device reading.
    for (BYTE pct : { BYTE(101), BYTE(255) }) {
        ctx = Poll(Coll(), { Answer(0x01, pct) });
        assert(ctx.out.empty() && ctx.claimed.empty());
    }

    // Ignore a stale answer for a different command; only accept the expected echo.
    ctx = Poll(Coll(), { Answer(0x01, 10, 0x07), Answer(0x01, 64) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 64 && reads == 2);

    // Feature reports are only touched on the exact vendor collection and interface 3.
    for (int fault = 0; fault < 7; ++fault) {
        auto c = Coll();
        if (fault == 0) c.vid = 0x3711;
        if (fault == 1) c.pid = 0x5405;
        if (fault == 2) c.iface = 2;
        if (fault == 3) c.caps.UsagePage = 0xFF00;
        if (fault == 4) c.caps.Usage = 0x0002;
        if (fault == 5) c.caps.InputReportByteLength = 64;
        if (fault == 6) c.caps.OutputReportByteLength = 64;
        ctx = Poll(c, { Answer(0x01, 76) });
        assert(ctx.out.empty() && ctx.claimed.empty() && opens == 0 && requests == 0);
    }

    // Interface 2 may exist at the same time, but must never be opened for this model.
    opens = requests = reads = 0;
    responses = { Answer(0x01, 90) };
    ctx = PollContext{};
    ctx.colls = { Coll(0x5404, 2), Coll(0x5404, 3) };
    PollPulsar(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 90 && opens == 1);

    // Other supported Pulsar dongles retain their existing generic naming/selection.
    ctx = Poll(Coll(0x5402, 3), { Answer(0x01, 53) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 53 && ctx.out[0].name == L"Pulsar wireless mouse");
    ctx = Poll(Coll(0x5403, 3), { Answer(0x01, 59) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 59 && ctx.out[0].name == L"Pulsar wireless mouse");

    openOk = false;
    ctx = Poll(Coll(), { Answer(0x01, 76) });
    assert(ctx.out.empty() && opens == 1 && requests == 0);
    openOk = true;
    writeOk = false;
    ctx = Poll(Coll(), { Answer(0x01, 76) });
    assert(ctx.out.empty() && requests == 1 && reads == 0);
    writeOk = true;
    ctx = Poll(Coll(), {});
    assert(ctx.out.empty() && ctx.claimed.empty());

    std::puts("Pulsar Feinmann FO1 tests passed (battery, pending, stale reply, bounds, offline, HID filtering, legacy dongles, I/O failures).");
}
