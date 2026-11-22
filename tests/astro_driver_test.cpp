#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>

namespace {
std::vector<std::vector<BYTE>> replies;
bool openOk = true, writeOk = true;
bool rejectReportId02 = false;           // legacy base declaring no report ID (some Gen 3 firmware)
std::vector<std::vector<BYTE>> writes;   // legacy (9886) frames written
int opens = 0;
HidColl Collection() {
    HidColl c;
    c.path = L"astro-test";
    c.vid = 0x046D; c.pid = 0x0B0B;
    c.caps.UsagePage = 0xFF32; c.caps.Usage = 0x74;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}
std::vector<BYTE> Reply(BYTE level, BYTE dock = 0) {
    return {2, 12, 6, 0, 6, 12, level, 100, dock};
}
PollContext Poll(std::vector<std::vector<BYTE>> data, HidColl c = Collection()) {
    replies = std::move(data);
    PollContext ctx; ctx.colls = {c}; PollAstro(ctx); return ctx;
}
}
std::wstring Hex4(USHORT v) { wchar_t b[8]; swprintf(b, 8, L"%04X", v); return b; }  // used by the Gen 3 / Gen 4 / A20 paths
bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
std::vector<HidColl> EnumHid() { return {}; }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    if (c_.vid == 0x9886) {
        writes.push_back(data);
        return !(rejectReportId02 && data[0] == 0x02);
    }
    std::vector<BYTE> expected(64, 0);
    expected[0] = 2; expected[1] = 12; expected[2] = 3;
    expected[4] = 6; expected[5] = 12;
    assert(data == expected); return writeOk;
}
int HidDev::Read(std::vector<BYTE>& out, int timeout) {
    assert(timeout > 0 && timeout <= 1000);
    if (replies.empty()) return -1;
    out = replies.front(); replies.erase(replies.begin()); return (int)out.size();
}
int main() {
    auto ctx = Poll({Reply(99)});
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 99 && !ctx.out[0].charging);
    assert(ctx.out[0].name == L"ASTRO A50 X" && ctx.out[0].kind == DeviceKind::Headset);
    assert(ctx.IsClaimed(0x046D, 0x0B0B));
    int before = opens; PollAstro(ctx); assert(opens == before);
    ctx = Poll({Reply(0, 1)});
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0 && ctx.out[0].charging);
    auto push = Reply(100); push[5] = 0;
    ctx = Poll({push}); assert(ctx.out.size() == 1 && ctx.out[0].percent == 100);
    auto unrelated = Reply(50); unrelated[4] = 8;
    ctx = Poll({unrelated, Reply(60)}); assert(ctx.out.size() == 1 && ctx.out[0].percent == 60);
    for (int field : {0, 1, 2, 3, 4, 5, 6}) {
        auto bad = Reply(50); bad[field] = 255;
        ctx = Poll({bad}); assert(ctx.out.empty() && ctx.claimed.empty());
    }
    ctx = Poll({{2, 12, 6, 0, 6, 12, 50}}); assert(ctx.out.empty());
    ctx = Poll({}); assert(ctx.out.empty());
    auto c = Collection(); c.pid = 0x0B1C;
    ctx = Poll({Reply(75)}, c); assert(ctx.out.size() == 1 && ctx.out[0].name == L"ASTRO A50 Gen 5");
    for (int field = 0; field < 5; ++field) {
        c = Collection();
        if (field == 0) c.vid = 0x1234;
        if (field == 1) c.pid = 0x1234;
        if (field == 2) c.caps.UsagePage = 0xFF00;
        if (field == 3) c.caps.Usage = 1;
        if (field == 4) c.caps.OutputReportByteLength = 20;
        before = opens; ctx = Poll({Reply(50)}, c);
        assert(ctx.out.empty() && opens == before);
    }
    openOk = false; ctx = Poll({Reply(50)}); assert(ctx.out.empty());
    openOk = true; writeOk = false; ctx = Poll({Reply(50)}); assert(ctx.out.empty());
    writeOk = true;

    // Legacy VID 9886 bases (A50 Gen 3 / Gen 4 / A20)
    auto legacy = [](USHORT pid) {
        HidColl l; l.path = L"astro-legacy"; l.vid = 0x9886; l.pid = pid; l.iface = 6;
        l.caps.UsagePage = 0xFF32; l.caps.Usage = 0x74;
        l.caps.InputReportByteLength = l.caps.OutputReportByteLength = 64;
        return l;
    };
    // Gen 4-style base: report ID 02, 0x54 linked, 0x7C 72 %
    writes.clear();
    ctx = Poll({{2, 2, 1, 0x02}, {2, 2, 1, 0x48}}, legacy(0x0018));
    assert(ctx.out.size() == 1 && ctx.out[0].connected && ctx.out[0].percent == 72 && !ctx.out[0].charging);
    assert(writes.size() == 2 && writes[0] == std::vector<BYTE>({2, 0x54}) && writes[1] == std::vector<BYTE>({2, 0x7C}));
    // Gen 3 base without report ID: 02 frame rejected, retried behind a 00 byte, then remembered
    rejectReportId02 = true; writes.clear();
    ctx = Poll({{2, 2, 1, 0x03}, {2, 2, 1, 0x80 | 90}}, legacy(0x0015));
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 90 && ctx.out[0].charging && ctx.out[0].detail == L"docked");
    assert(writes.size() == 3 && writes[1] == std::vector<BYTE>({0, 2, 0x54}) && writes[2] == std::vector<BYTE>({0, 2, 0x7C}));
    writes.clear();
    ctx = Poll({{2, 2, 1, 0x02}, {2, 2, 1, 40}}, legacy(0x0015));
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 40 && writes.size() == 2 && writes[0][0] == 0);
    rejectReportId02 = false;
    // Base that ignores 0x54 but answers 0x7C
    ctx = Poll({{}, {}, {2, 2, 1, 0x80 | 5}}, legacy(0x0014));
    assert(ctx.out.size() == 1 && ctx.out[0].connected && ctx.out[0].percent == 5 && ctx.out[0].charging);
    // Headset off: listed as disconnected
    ctx = Poll({{2, 2, 1, 0x00}}, legacy(0x001D));
    assert(ctx.out.size() == 1 && !ctx.out[0].connected && ctx.out[0].percent < 0);
    // 0x7C error (headset not linked) without 0x54 support: listed as disconnected
    ctx = Poll({{}, {}, {2, 1, 0, 0}}, legacy(0x0026));
    assert(ctx.out.size() == 1 && !ctx.out[0].connected);
    // A50 Gen 3 (9886:0015): 0x7C refused, battery from 0x72 01 field 04
    {
        std::vector<BYTE> ui = {2, 2, 16, 2, 1, 1, 0, 2, 2, 1, 0, 2, 3, 0x80, 0, 2, 4, 0x80 | 63, 0};
        std::vector<BYTE> refused = {2, 1, 0x1F, 1, 0, 0, 0, 'H', 'I', 'D', '_', 'E'};
        writes.clear();
        ctx = Poll({{2, 2, 1, 0x02}, refused, ui}, legacy(0x0015 + 0x100));  // fresh framing cache
        assert(ctx.out.empty());  // unknown PID: not polled
        HidColl g3 = legacy(0x0015);
        ctx = Poll({{2, 2, 1, 0x02}, refused, ui}, g3);
        assert(ctx.out.size() == 1 && ctx.out[0].connected && ctx.out[0].percent == 63 && ctx.out[0].charging);
        assert(writes.back() == std::vector<BYTE>({0, 2, 0x72, 1, 1}) || writes.back() == std::vector<BYTE>({2, 0x72, 1, 1}));
        // Wrong field marker: no battery shown, but headset still listed as linked
        auto bad = ui; bad[16] = 3;
        ctx = Poll({{2, 2, 1, 0x02}, refused, bad}, g3);
        assert(ctx.out.size() == 1 && ctx.out[0].connected && ctx.out[0].percent < 0);
    }
    // Nothing answers: not listed
    ctx = Poll({}, legacy(0x002C));
    assert(ctx.out.empty() && ctx.claimed.empty());
    puts("Astro driver tests passed");
}
