// Exercise the real Pulsar battery poller with simulated HID feature reports.
// Does not touch physical hardware or send real device commands.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <deque>

namespace {
std::deque<std::vector<BYTE>> responses;
int opens = 0, requests = 0, reads = 0, batteryRequests = 0;
bool openOk = true, writeOk = true;
bool sticky = false;  // the last queued reply stays in the buffer, as on the device

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
    opens = requests = reads = batteryRequests = 0;
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
// The PRO Dongle / ZYWOO path (input/output reports) is not exercised here: these collections never match it.
bool HidDev::Open(const HidColl&) { assert(false && "unexpected input/output open"); return false; }
int HidDev::WriteAndRead(const std::vector<BYTE>&, std::vector<BYTE>&, int, bool*) { return -1; }
int HidDev::Read(std::vector<BYTE>&, int) { return -1; }
bool HidDev::SetFeature(const std::vector<BYTE>& q) {
    ++requests;
    assert(q.size() == 65 && q[0] == 0);
    // Only the three reads are ever sent: battery 08 81 01, and on the 1K dongle charging 08 83 01 and the
    // paired mouse 01 84 04 (PR #164).
    const bool battery = q[2] == 0x08 && q[3] == 0x81 && q[4] == 0x01;
    const bool charge = q[2] == 0x08 && q[3] == 0x83 && q[4] == 0x01;
    const bool paired = q[2] == 0x01 && q[3] == 0x84 && q[4] == 0x04;
    assert(q[1] == 0x00 && (battery || charge || paired));
    for (size_t i = 5; i < 63; ++i) assert(q[i] == 0);
    assert(q[63] == (BYTE)(q[2] + q[3] + q[4]) && q[64] == 0);
    if (battery) ++batteryRequests;
    return writeOk;
}
bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    ++reads;
    assert(id == 0x00);
    if (responses.empty()) return false;
    out = responses.front();
    if (!sticky || responses.size() > 1) responses.pop_front();
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

    // 1K dongle 5402. The first poll reads the buffer before sending: `idle` is an earlier, final answer.
    const auto idle = Answer(0x01, 0x10, 0x01, 0x84, 0x04);
    ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x01, 53) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 53 && ctx.out[0].name == L"Pulsar wireless mouse");
    assert(!ctx.out[0].charging && ctx.out[0].connected && !ctx.out[0].asleep);
    // The charging read failed, so the paired-mouse read checks the buffer first and fails there too.
    assert(requests == 2 && ctx.IsClaimed(0x3710, 0x5402));
    // The charging register and the paired mouse's id (an X3, 3710:3409) name and flag it.
    auto paired = Answer(0x01, 0x10, 0x01, 0x84, 0x04);
    paired[8] = 0x37;  // VID 3710 little endian in payload bytes 6..7
    paired[9] = 0x09;
    paired[10] = 0x34;  // PID 3409 in payload bytes 8..9
    ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x01, 41), Answer(0x01, 0x01, 0x08, 0x83, 0x01), paired });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 41 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"Pulsar X3");
    // 05 ends the read at once: asleep, last value kept, named by the dongle; no charging read.
    ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x02, 0), Answer(0x05, 0), paired });
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].connected && ctx.out[0].percent == -1);
    assert(ctx.out[0].name == L"Pulsar X3" && requests == 2 && batteryRequests == 1);
    // A pending battery read is waited for, never re-sent.
    {
        std::deque<std::vector<BYTE>> q = { idle };
        for (int i = 0; i < 20; ++i) q.push_back(Answer(0x02, 0));
        q.push_back(Answer(0x01, 77));
        ctx = Poll(Coll(0x5402, 3), q);
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 77 && batteryRequests == 1);
    }
    // 0 and FF: nothing paired or in range.
    for (BYTE pct : { BYTE(0), BYTE(255) }) {
        ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x01, pct) });
        assert(ctx.out.size() == 1 && !ctx.out[0].connected && ctx.out[0].percent == -1);
    }
    // A dongle left pending on 02 by someone else gets nothing more: no request, asleep, still claimed.
    sticky = true;
    ctx = Poll(Coll(0x5402, 3), { Answer(0x02, 0, 0x08, 0x83, 0x01) });
    assert(requests == 0 && ctx.out.size() == 1 && ctx.out[0].asleep && ctx.IsClaimed(0x3710, 0x5402));
    // Our own pending read never answered: one request in the 1 s deadline, then asleep.
    ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x02, 0) });
    assert(batteryRequests == 1 && requests == 1 && ctx.out.size() == 1 && ctx.out[0].asleep);
    // The buffer went back to the earlier answer after showing ours pending: the request was lost, so it is
    // re-sent, at most twice.
    ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x02, 0), idle });
    assert(batteryRequests == 3 && ctx.out.size() == 1 && ctx.out[0].asleep);
    sticky = false;
    // Claimed even when it cannot be opened or no collection matches, so kysona.cpp never probes it.
    openOk = false;
    ctx = Poll(Coll(0x5402, 3), { idle, Answer(0x01, 53) });
    assert(ctx.out.empty() && requests == 0 && ctx.IsClaimed(0x3710, 0x5402));
    openOk = true;
    ctx = Poll(Coll(0x5402, 2), { idle, Answer(0x01, 53) });
    assert(ctx.out.empty() && opens == 0 && ctx.IsClaimed(0x3710, 0x5402));
    // Other supported Pulsar dongles retain their existing generic naming/selection.
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
