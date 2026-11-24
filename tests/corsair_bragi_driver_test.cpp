#include "../src/drivers/drivers.h"
#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
bool plain = false, lateReplies = false, receiverOnly = false;
BYTE failedSource = 0xFF;
int slots = 1, opens = 0;
std::vector<std::vector<BYTE>> replies;

std::vector<BYTE> Reply(BYTE source, unsigned value, BYTE status = 0) {
    std::vector<BYTE> r = {source, 0x02, status, (BYTE)value, (BYTE)(value >> 8),
                           (BYTE)(value >> 16), (BYTE)(value >> 24)};
    if (!plain) r.insert(r.begin(), 0x01);  // numbered HS80 MAX input report
    return r;
}

PollContext Poll() {
    HidColl c;
    c.vid = 0x1B1C; c.pid = 0x0A97; c.iface = 4;
    c.caps.UsagePage = 0xFF42; c.caps.Usage = 1;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = plain ? 65 : 64;
    PollContext ctx; ctx.colls = {c}; PollCorsair(ctx); return ctx;
}
}

bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return true; }
void HidDev::Close() {}
void HidDev::Flush() { replies.clear(); }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    if (data.size() == 5) {
        assert(data[0] == 0 && data[1] == 2 && data[3] == 2);
        return false;  // unnumbered collection trying legacy framing first
    }
    assert(data.size() == 4);
    assert(data[2] == 0x02);  // GET only; never SET or change modes
    if (data[0] != (plain ? 0x00 : 0x02)) return false;
    BYTE source = data[1] - 0x08;
    BYTE prop = data[3];
    assert(source <= 3 && (prop == 0x0F || prop == 0x10 || prop == 0x12));
    // A delayed response from the first headset can arrive after a flush, while
    // probing another slot. It must not become that slot's battery, PID or charge state.
    if (lateReplies && source > 1) replies.push_back(Reply(1, prop == 0x0F ? 990 : 1));
    bool present = receiverOnly ? source == 0 : source > 0 && source <= slots;
    if (present) {
        unsigned value = prop == 0x0F ? (source ? source * 100 + 320 : 650) :
                         prop == 0x12 ? 0x0A96 : 2;
        replies.push_back(Reply(source, value, source == failedSource ? 5 : 0));
    }
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeout) {
    assert(timeout > 0 && timeout <= 300);
    if (replies.empty()) return -1;
    out = replies.front(); replies.erase(replies.begin()); return (int)out.size();
}
std::wstring HidProduct(const HidColl&) { return L"CORSAIR HS80 MAX WIRELESS Gaming Receiver"; }
std::wstring Hex4(USHORT v) { wchar_t b[8]; swprintf(b, 8, L"%04X", v); return b; }
int Clamp100(int v) { return std::clamp(v, 0, 100); }
DeviceKind GuessKind(const std::wstring&) { return DeviceKind::Other; }
DeviceKind KindFromCollections(const std::vector<HidColl>&, USHORT, USHORT) { return DeviceKind::Other; }

int main() {
    for (bool framing : {false, true}) {
        plain = framing; lateReplies = false; receiverOnly = false; slots = 1;
        auto ctx = Poll();
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 42);
        assert(ctx.out[0].key == L"corsair:0A97:9" && !ctx.out[0].charging);
        assert(ctx.IsClaimed(0x1B1C, 0x0A97));
        int before = opens; PollCorsair(ctx); assert(opens == before);

        lateReplies = true;
        ctx = Poll();
        assert(ctx.out.size() == 1);  // no phantom slots 10 / 11 from source 1
        assert(ctx.out[0].percent == 42);

        slots = 3;
        ctx = Poll();
        assert(ctx.out.size() == 3);
        for (int i = 0; i < 3; ++i) {
            assert(ctx.out[i].key == L"corsair:0A97:" + std::to_wstring(9 + i));
            assert(ctx.out[i].percent == 42 + i * 10 && !ctx.out[i].charging);
            assert(ctx.out[i].kind == DeviceKind::Headset);
        }

        failedSource = 2;
        ctx = Poll();
        assert(ctx.out.size() == 2 && ctx.out[1].key == L"corsair:0A97:11");
        failedSource = 0xFF;

        receiverOnly = true; lateReplies = false;
        ctx = Poll();
        assert(ctx.out.size() == 1 && ctx.out[0].key == L"corsair:0A97:8");
        assert(ctx.out[0].percent == 65);
    }
    std::puts("Corsair Bragi driver tests passed");
}
