// Exercise the production SONiX poller without accessing HID hardware.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
#include <deque>
#include <map>

static std::deque<std::vector<BYTE>> replies;
static int opens, writes;
static bool openOk = true, writeOk = true;
bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& request, int ms) {
    std::vector<BYTE> expected(33, 0);
    expected[1] = 0x20; expected[2] = 1; expected[32] = 0x21;
    assert(request == expected && ms == 500);
    ++writes;
    return writeOk;
}
int HidDev::Read(std::vector<BYTE>& reply, int ms) {
    assert(ms > 0 && ms <= 2000);  // 2000: the Lofree LF2-v1 wait
    if (replies.empty()) return -1;
    reply = replies.front(); replies.pop_front();
    return reply.empty() ? -1 : static_cast<int>(reply.size());
}
std::wstring HidProduct(const HidColl&) { return L"Keyboard 2.4G"; }
std::wstring Hex4(USHORT value) {
    wchar_t result[5]; swprintf(result, 5, L"%04X", value); return result;
}
static HidColl Collection(USHORT vid = 0x342D, USHORT pid = 0xE479, int iface = 1) {
    HidColl c; c.vid = vid; c.pid = pid; c.iface = iface;
    c.caps.UsagePage = 0xFF60; c.caps.Usage = 0x61;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 33;
    return c;
}
static PollContext Poll(HidColl collection, std::deque<std::vector<BYTE>> input) {
    opens = writes = 0; replies = input;
    PollContext ctx; ctx.colls = {collection}; PollSonix(ctx); return ctx;
}
int main() {
    for (auto id : {std::pair<USHORT, USHORT>{0x05AC, 0x024F},
        {0x0C45, 0xFEFE},
        {0x0C45, 0xFDFD},
        {0x342D, 0xE479},
        {0x342D, 0xE411}, // RSQ_20055_2.4G (issue #3808, interface 1)
        {0x342D, 0xE489}, // EPOMAKER Galaxy100 (#3802): FF60/0061 on interface 2
        {0x342D, 0xE485},
        {0x342D, 0xE4BC},
        {0x342D, 0xE4C2},
        {0x342D, 0xE407},
        {0x388D, 0x0003},
        {0x0DB0, 0x0CA0},
        {0x36B0, 0x30BD},
        {0x342D, 0xE51A},
        {0x342D, 0xE487},
        {0x342D, 0xE537},
        {0x19F5, 0x3245},
        {0x362D, 0xD028},
        {0x362D, 0xD030},
        {0x362D, 0xD101},
        {0x342D, 0xE49F}}) {
        int iface = (id.first == 0x05AC || id.first == 0x0C45) ? 3 :
                    (id.first == 0x342D && id.second == 0xE489) ? 2 : 1;
        auto ctx = Poll(Collection(id.first, id.second, iface), {{0x20, 1, 0, 73}});
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 73);
        assert(ctx.out[0].key == L"sonix:" + Hex4(id.first) + L":" + Hex4(id.second));
        assert(ctx.out[0].name == L"Keyboard" && ctx.out[0].graceSec == 1800);
        PollSonix(ctx); assert(opens == 1 && writes == 1);
    }
    // RSQ_20055_2.4G reports SONiX FF60:0061 on MI_01 only.
    for (int iface : {0, 2, 3, 4}) {
        auto ctx = Poll(Collection(0x342D, 0xE411, iface), {{0x20, 1, 0, 73}});
        assert(ctx.out.empty() && opens == 0 && writes == 0);
    }
    auto wrongSize = Collection(0x342D, 0xE411, 1);
    --wrongSize.caps.InputReportByteLength;
    assert(Poll(wrongSize, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    wrongSize = Collection(0x342D, 0xE411, 1);
    --wrongSize.caps.OutputReportByteLength;
    assert(Poll(wrongSize, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    // Galaxy100 is matched only on its reported SONiX vendor HID interface (MI_02).
    // In particular, do not probe its other QMK/VIA interface numbers.
    for (int iface : {0, 1, 3}) {
        auto ctx = Poll(Collection(0x342D, 0xE489, iface), {{0x20, 1, 0, 73}});
        assert(ctx.out.empty() && opens == 0 && writes == 0);
    }
    wrongSize = Collection(0x342D, 0xE489, 2);
    --wrongSize.caps.OutputReportByteLength;
    assert(Poll(wrongSize, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    for (int level : {0, 1, 73, 100, 101, 255}) {
        auto ctx = Poll(Collection(), {{0x20, 1, 0, static_cast<BYTE>(level)}});
        bool connected = level >= 1 && level <= 100;
        assert(ctx.out.size() == 1 && ctx.out[0].connected == connected);
        assert(ctx.out[0].percent == (connected ? level : -1));
    }
    for (int fault = 0; fault < 8; ++fault) {
        auto c = Collection();
        if (fault == 0) c.vid = 0xFFFF;
        if (fault == 1) c.pid = 0xFFFF;
        if (fault == 2) ++c.caps.UsagePage;
        if (fault == 3) ++c.caps.Usage;
        if (fault == 4) --c.caps.InputReportByteLength;
        if (fault == 5) --c.caps.OutputReportByteLength;
        if (fault == 6) c = Collection(0x05AC, 0x024F, 1);
        if (fault == 7) c = Collection(0x36B0, 0x3002, 1); // PMO/VIA shared ID stays excluded.
        assert(Poll(c, {{0x20, 1, 0, 73}}).out.empty() && opens == 0);
    }
    auto noisy = Poll(Collection(), {{0x20}, {0, 1, 0, 73},
        {0x20, 2, 0, 73},
        {0x20, 1, 1, 73},
        {0x20, 1, 0, 73}});
    assert(noisy.out.size() == 1 && writes == 1);
    assert(Poll(Collection(), {{},
        {0x20, 1, 0, 73}}).out.size() == 1 && writes == 2);
    assert(Poll(Collection(), {}).out.empty() && writes == 2);
    openOk = false;
    assert(Poll(Collection(), {}).out.empty() && writes == 0);
    openOk = true; writeOk = false;
    assert(Poll(Collection(), {}).out.empty() && writes == 2);
    // Lofree Flow 2 (388D:0002): LF2-v1 replies only, and only with the receiver's interface-0 collection.
    auto flowVia = Collection(0x388D, 0x0002, 2);
    HidColl flowRx; flowRx.vid = 0x388D; flowRx.pid = 0x0002; flowRx.iface = 0; flowRx.path = L"rx";
    flowRx.caps.UsagePage = 0x0001; flowRx.caps.Usage = 0;
    flowRx.caps.InputReportByteLength = flowRx.caps.OutputReportByteLength = 64;
    flowVia.path = L"via";
    auto lf2 = [](BYTE level, BYTE status) {
        std::vector<BYTE> r(32, 0);
        r[0] = 0x20; r[1] = 1; r[3] = level; r[4] = status; r[5] = 'L'; r[6] = 'F'; r[7] = '2'; r[8] = 1;
        unsigned sum = 0;
        for (int i = 0; i < 31; i++) sum += r[i];
        r[31] = (BYTE)sum;
        return r;
    };
    auto pollFlow = [&](std::deque<std::vector<BYTE>> input, bool withReceiver = true) {
        openOk = writeOk = true;
        opens = writes = 0; replies = input;
        PollContext ctx; ctx.colls = {flowVia};
        if (withReceiver) ctx.colls.push_back(flowRx);
        PollSonix(ctx);
        return ctx;
    };
    auto flow = pollFlow({lf2(0, 1)});  // 0% is a real LF2-v1 reading
    assert(flow.out.size() == 1 && flow.out[0].percent == 0 && flow.out[0].name == L"Lofree Flow 2");
    flow = pollFlow({lf2(64, 1)});
    assert(flow.out.size() == 1 && flow.out[0].percent == 64 && flow.out[0].key == L"sonix:388D:0002");
    flow = pollFlow({lf2(0xFF, 0)});  // no reading yet: shown without a level
    assert(flow.out.size() == 1 && flow.out[0].percent == -1);
    auto badSum = lf2(50, 1); badSum[31] ^= 1;
    assert(pollFlow({badSum}).out.empty());
    assert(pollFlow({lf2(50, 1)}, false).out.empty() && opens == 0);  // cable: not the receiver
    // Stock firmware echoes the request without the LF2 signature: never a level, and not asked again.
    assert(pollFlow({{0x20, 1, 0, 73}}).out.empty() && writes == 1);
    assert(pollFlow({lf2(50, 1)}).out.empty() && opens == 0);
    pollFlow({}, false);  // receiver unplugged: the next one is asked again
    assert(pollFlow({lf2(50, 1)}).out.size() == 1);
    puts("SONiX driver tests passed (21 receiver IDs, RSQ and Galaxy100 interface filtering, Lofree Flow 2 LF2-v1)");
}
