// Exercise the production BeiYing driver (EWEADN S7 / S9, RK M3) with simulated feature reports; no hardware access.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

namespace {

std::vector<std::vector<BYTE>> pending;  // GetFeature answers, in order
std::vector<std::vector<BYTE>> sent;     // SetFeature requests
int opens = 0;

// The FF00:0001 vendor collection on interface 2: 64-byte feature reports only.
HidColl Collection(USHORT pid) {
    HidColl c;
    c.path = L"beiying-ff00";
    c.vid = 0x372E;
    c.pid = pid;
    c.caps.UsagePage = 0xFF00;
    c.caps.Usage = 0x0001;
    c.caps.FeatureReportByteLength = 64;
    return c;
}

std::vector<BYTE> Frame(std::initializer_list<BYTE> head) {
    std::vector<BYTE> r(head);
    r.resize(64, 0);
    return r;
}

// Captured from a real RK M3 on its 2.4 GHz dongle (372E:1019): 34 %, not charging.
std::vector<BYTE> RkReply(BYTE basics = 0x22, BYTE sn = 0x02, BYTE online = 0x01) {
    return Frame({ 0x03, 0x50, 0x00, 0x0B, sn, online, basics, 0x21, 0x07, 0x01, 0x30, 0x75, 0x08, 0xA5, 0x5A });
}

// EWEADN replies: 03 50 <ack> <len> 00 <sub> <data>.
std::vector<BYTE> EweadnReply(BYTE sub, BYTE data) { return Frame({ 0x03, 0x50, 0x00, 0x01, 0x00, sub, data }); }

// Captured from a real S9 Pro 8K on its 2.4 GHz receiver (372E:1026, firmware v1052):
// 03 50 00 0B 00 01 5D ... -> 0x5D = 93 %, bit 7 clear.
std::vector<BYTE> EweadnBasicsReply(BYTE basics) {
    return Frame({ 0x03, 0x50, 0x00, 0x0B, 0x00, 0x01, basics, 0x21, 0x07, 0x01, 0x90, 0x65, 0x04, 0xA5, 0x5A, 0x00 });
}

PollContext Poll(USHORT pid, std::vector<std::vector<BYTE>> replies) {
    pending = std::move(replies);
    sent.clear();
    PollContext ctx;
    ctx.colls = { Collection(pid) };
    PollEweadn(ctx);
    return ctx;
}

}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
bool HidDev::OpenFeatureOnly(const HidColl& c) {
    ++opens;
    c_ = c;
    return true;
}
void HidDev::Close() {}
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    sent.push_back(data);
    return true;
}
bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    assert(reportId == 0x03);
    if (pending.empty()) return false;
    out = pending.front();
    pending.erase(pending.begin());
    return true;
}

std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    // RK M3 on its dongle: one read-only "get basics" request, exactly as RK's web driver builds it.
    auto ctx = Poll(0x1019, { RkReply() });
    assert(sent.size() == 1);
    const std::vector<BYTE> expected = Frame({ 0x03, 0x22, 0x50, 0x00, 0x02, 0x4F, 0x81 });
    assert(sent[0] == expected);
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x372E, 0x1019));
    const auto& s = ctx.out[0];
    assert(s.percent == 34 && !s.charging && s.connected);
    assert(s.name == L"RK M3" && s.key == L"rk:1019");  // stable across USB ports, unlike the HID path
    assert(s.kind == DeviceKind::Mouse && s.via == L"2.4 GHz receiver");

    // Bit 7 is the charging flag.
    ctx = Poll(0x1019, { RkReply(0xA2) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 34 && ctx.out[0].charging);

    // The RK M3 on its USB cable uses the same request.
    ctx = Poll(0x102A, { RkReply(0x64) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].via == L"USB cable");

    // Not trusted: the mouse did not answer (byte 5 != 1), a reply to another request, an impossible level.
    ctx = Poll(0x1019, { RkReply(0x22, 0x02, 0x00), RkReply(0x22, 0x02, 0x00), RkReply(0x22, 0x02, 0x00) });
    assert(ctx.out.empty() && ctx.claimed.empty());
    ctx = Poll(0x1019, { RkReply(0x22, 0x00), RkReply(0x22, 0x00), RkReply(0x22, 0x00) });
    assert(ctx.out.empty());
    ctx = Poll(0x1019, { RkReply(0x65) });
    assert(ctx.out.empty());

    // A stale first read is retried.
    ctx = Poll(0x1019, { RkReply(0x22, 0x00), RkReply(0x30) });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 48);

    // EWEADN receiver (unchanged behaviour): link check 47 00, then 4F 81, both with sn 0.
    ctx = Poll(0x103D, { EweadnReply(0x00, 0x01), EweadnReply(0x81, 0x80 | 57) });
    assert(sent.size() == 2 && sent[0][4] == 0x00 && sent[0][5] == 0x47 && sent[1][5] == 0x4F && sent[1][6] == 0x81);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 57 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"EWEADN wireless mouse" && ctx.out[0].key == L"eweadn:beiying-ff00");

    // EWEADN receiver with the mouse off: reported as disconnected, no level asked.
    ctx = Poll(0x103D, { EweadnReply(0x00, 0x00) });
    assert(sent.size() == 1 && ctx.out.size() == 1 && !ctx.out[0].connected);

    // EWEADN S9 Ultra cable: no link check.
    ctx = Poll(0x1027, { EweadnReply(0x81, 80) });
    assert(sent.size() == 1 && ctx.out.size() == 1 && ctx.out[0].percent == 80 && ctx.out[0].name == L"EWEADN S9 Ultra");

    // S9 Pro 8K receiver (372E:1026): the same link check, then the real 11-byte basics reply.
    ctx = Poll(0x1026, { EweadnReply(0x00, 0x01), EweadnBasicsReply(0x5D) });
    assert(sent.size() == 2 && sent[0][5] == 0x47 && sent[1][5] == 0x4F && sent[1][6] == 0x81);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 93 && !ctx.out[0].charging);
    assert(ctx.out[0].name == L"EWEADN wireless mouse" && ctx.out[0].via == L"2.4 GHz receiver");

    // The other S9 receiver ids share the same protocol.
    for (USHORT pid : { 0x1033, 0x1055 }) {
        ctx = Poll(pid, { EweadnReply(0x00, 0x01), EweadnReply(0x81, 64) });
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 64 && ctx.IsClaimed(0x372E, pid));
    }

    // S9 Pro / Max / Ultra+ on their USB cables (S9 Ultra is covered above).
    ctx = Poll(0x1029, { EweadnReply(0x81, 74) });
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"EWEADN S9 Pro" && ctx.out[0].via == L"USB cable");
    assert(ctx.out[0].percent == 74);
    ctx = Poll(0x105E, { EweadnReply(0x81, 55) });
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"EWEADN S9 Max" && ctx.out[0].percent == 55);
    ctx = Poll(0x108D, { EweadnReply(0x81, 44) });
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"EWEADN S9 Ultra+" && ctx.out[0].percent == 44);

    // Other 372E products (AULA, VGN, unknown) are never opened.
    int before = opens;
    for (USHORT pid : { 0x106C, 0x10BB, 0x1000 }) {
        ctx = Poll(pid, { RkReply() });
        assert(ctx.out.empty() && sent.empty());
    }
    assert(opens == before);

    puts("beiying driver: all tests passed");
}
