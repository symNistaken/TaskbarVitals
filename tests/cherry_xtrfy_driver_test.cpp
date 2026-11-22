// Exercise the Cherry XTRFY 1K Mouse Receiver driver with simulated replies; no hardware access.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

namespace {
std::vector<std::vector<std::vector<BYTE>>> script;  // replies queued per Write call
std::vector<std::vector<BYTE>> writes, pending;
int opens = 0;
bool openOk = true;

HidColl Collection(USHORT pid = 0x0330, USHORT page = 0xFF02, USHORT usage = 0x0002, USHORT in = 17, USHORT out = 17) {
    HidColl c;
    c.path = L"test-receiver";
    c.vid = 0x046A;
    c.pid = pid;
    c.iface = 1;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = in;
    c.caps.OutputReportByteLength = out;
    return c;
}

std::vector<BYTE> Reply(BYTE cmd, BYTE value6, BYTE charging = 0, BYTE mvHi = 0, BYTE mvLo = 0) {
    std::vector<BYTE> r(17, 0);
    r[0] = 0x08;
    r[1] = cmd;
    r[5] = 0x02;
    r[6] = value6;
    r[7] = charging;
    r[8] = mvHi;
    r[9] = mvLo;
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) sum += r[i];
    r[16] = (BYTE)(0x55 - sum);
    return r;
}

PollContext Poll(std::vector<std::vector<std::vector<BYTE>>> perWrite, std::vector<HidColl> colls = { Collection() }) {
    script = std::move(perWrite);
    writes.clear();
    pending.clear();
    opens = 0;
    PollContext ctx;
    ctx.colls = std::move(colls);
    PollCherry(ctx);
    return ctx;
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    if (!openOk) return false;
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = true;
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
void HidDev::Flush() { pending.clear(); }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    writes.push_back(data);
    size_t i = writes.size() - 1;
    if (i < script.size()) pending = script[i];
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 500);
    if (pending.empty()) return -1;
    out = pending.front();
    pending.erase(pending.begin());
    return (int)out.size();
}
std::wstring Hex4(USHORT v) {
    wchar_t s[8];
    swprintf(s, 8, L"%04X", v);
    return s;
}

int main() {
    // The two frames ever sent: report 08, command 03 then 04, checksum 0x55 - sum(bytes 0..15).
    std::vector<BYTE> link(17, 0), power(17, 0);
    link[0] = power[0] = 0x08;
    link[1] = 0x03;
    power[1] = 0x04;
    link[16] = (BYTE)(0x55 - 0x08 - 0x03);
    power[16] = (BYTE)(0x55 - 0x08 - 0x04);

    // Normal reply: 95%, on battery, 4084 mV (the live capture from the real receiver).
    auto ctx = Poll({ { Reply(0x03, 1) }, { Reply(0x04, 95, 0, 0x0F, 0xF4) } });
    assert(writes.size() == 2 && writes[0] == link && writes[1] == power);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].percent == 95 && !ctx.out[0].charging && !ctx.out[0].asleep);
    assert(ctx.out[0].millivolts == 4084);
    assert(ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].name == L"Cherry XTRFY wireless mouse");
    assert(ctx.out[0].key == L"cherry:xtrfy");
    assert(ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.out[0].graceSec > 0);
    assert(ctx.IsClaimed(0x046A, 0x0330));

    // Charging flag is passed through.
    ctx = Poll({ { Reply(0x03, 1) }, { Reply(0x04, 100, 1, 0x10, 0x94) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].charging && ctx.out[0].millivolts == 0x1094);

    // Replies to other commands or with a bad checksum are skipped until the real one arrives.
    std::vector<BYTE> wrong = Reply(0x04, 42);
    wrong[6] = 43;  // checksum no longer matches
    ctx = Poll({ { Reply(0x03, 1) }, { Reply(0x09, 7), wrong, Reply(0x04, 42) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42);

    // Mouse switched off: the receiver answers the link query with 0, power is not asked, nothing
    // is reported but the receiver is claimed so the fallbacks stay away.
    ctx = Poll({ { Reply(0x03, 0) } });
    assert(writes.size() == 1 && ctx.out.empty() && ctx.IsClaimed(0x046A, 0x0330));

    // Receiver fully silent (asleep mouse or another driver holding the channel): nothing reported,
    // nothing claimed.
    ctx = Poll({});
    assert(ctx.out.empty() && !ctx.IsClaimed(0x046A, 0x0330));

    // Linked but silent on power: shown as asleep, keeping the last value.
    ctx = Poll({ { Reply(0x03, 1) }, {} });
    assert(writes.size() == 2 && ctx.out.size() == 1 && ctx.out[0].percent == -1 && ctx.out[0].asleep);

    // Malformed frames are never misreported.
    ctx = Poll({ { std::vector<BYTE>{ 0x08, 0x04, 0x00 } }, { Reply(0x04, 61) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 61);

    // Exact matching: other product ids, usages and report sizes are never opened.
    ctx = Poll({ { Reply(0x03, 1), Reply(0x04, 80) } }, { Collection(0x00EA) });
    assert(opens == 0 && writes.empty() && ctx.out.empty());
    ctx = Poll({ { Reply(0x03, 1), Reply(0x04, 80) } }, { Collection(0x0330, 0xFF01, 0x0000) });
    assert(opens == 0 && writes.empty() && ctx.out.empty());
    ctx = Poll({ { Reply(0x03, 1), Reply(0x04, 80) } }, { Collection(0x0330, 0xFF02, 0x0002, 64, 64) });
    assert(opens == 0 && writes.empty() && ctx.out.empty());

    // The MX 3.0S keyboard and the XTRFY receiver each get their own channel and their own entry.
    auto KbReply = [](BYTE percent) {
        std::vector<BYTE> r(64, 0);
        r[0] = 0x04;
        r[3] = 0x1A;
        r[8] = percent;
        return r;
    };
    ctx = Poll({ { KbReply(70) }, { Reply(0x03, 1) }, { Reply(0x04, 55) } },
               { Collection(0x00EA, 0xFF1C, 0x0092, 64, 64), Collection() });
    assert(ctx.out.size() == 2);
    assert(ctx.out[0].kind == DeviceKind::Keyboard && ctx.out[0].percent == 70);
    assert(ctx.out[1].kind == DeviceKind::Mouse && ctx.out[1].percent == 55);

    // Already claimed by another driver: untouched.
    {
        writes.clear();
        opens = 0;
        PollContext claimed;
        claimed.colls = { Collection() };
        claimed.Claim(0x046A, 0x0330);
        PollCherry(claimed);
        assert(opens == 0 && writes.empty() && claimed.out.empty());
    }

    // Open failure (another program holds the channel): no device, no crash.
    openOk = false;
    ctx = Poll({ { Reply(0x03, 1), Reply(0x04, 80) } });
    assert(ctx.out.empty() && writes.empty());
    openOk = true;

    std::puts("cherry xtrfy driver test passed");
    return 0;
}
