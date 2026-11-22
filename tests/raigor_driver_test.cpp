// Exercise the production Raigor driver without accessing hardware.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
#include <deque>
static std::deque<std::vector<BYTE>> frames;
static int writes, reads, opens;
static bool openOk = true, writeOk = true;
bool HidDev::Open(const HidColl& c) { c_ = c; ++opens; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& q, int ms) {
    assert((q == std::vector<BYTE>{0xB5, 5, 0, 0, 0, 0, 0, 0}));
    assert(ms == 500); ++writes; return writeOk;
}
int HidDev::Read(std::vector<BYTE>& r, int ms) {
    assert(ms > 0 && ms <= 200); ++reads;
    if (frames.empty()) return -1;
    r = frames.front(); frames.pop_front(); return static_cast<int>(r.size());
}
std::wstring HidProduct(const HidColl&) { return L"Raigor III Wireless Mouse"; }
std::wstring Hex4(USHORT n) { wchar_t b[5]; swprintf(b, 5, L"%04X", n); return b; }
DeviceKind KindFromCollections(const std::vector<HidColl>&, USHORT, USHORT) { return DeviceKind::Mouse; }
static HidColl Collection(USHORT pid = 0x0066) {
    HidColl c; c.vid = 0x1EA7; c.pid = pid;
    c.caps.UsagePage = 0xFFB5; c.caps.Usage = 1;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 8; return c;
}
static std::vector<BYTE> Page(BYTE level) { return {0xB5, 5, 0x94, 4, 0x35, 9, level, 0x38}; }
static PollContext Poll(HidColl c, std::deque<std::vector<BYTE>> f) {
    frames = f; opens = writes = reads = 0;
    PollContext ctx; ctx.colls = {c}; PollRaigor(ctx); return ctx;
}
int main() {
    for (USHORT pid : {USHORT(0x0066), USHORT(0x0064)}) {
        for (BYTE level : {BYTE(0), BYTE(64), BYTE(100)}) {
            auto ctx = Poll(Collection(pid), {Page(level)});
            assert(ctx.out.size() == 1 && ctx.out[0].percent == level && !ctx.out[0].charging);
            assert(ctx.out[0].key == L"raigor:1EA7:" + Hex4(pid));
            assert(ctx.out[0].graceSec == 1800);
            PollRaigor(ctx); assert(opens == 1 && writes == 1);
        }
    }
    auto ctx = Poll(Collection(), {{0xB5, 0x67, 1, 2, 3, 4, 9, 42}});
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 42);
    for (int fault = 0; fault < 5; ++fault) {
        auto bad = Page(64);
        if (fault == 0) bad[0] = 0;
        if (fault == 1) bad[1] = 0;
        if (fault == 2) bad[2] = 0;
        if (fault == 3) bad[3] = 0;
        if (fault == 4) bad[6] = 101;
        assert(Poll(Collection(), {bad}).out.empty());
    }
    assert(Poll(Collection(), {{0xB5}, Page(64)}).out.size() == 1);
    assert(Poll(Collection(), {{0xB5, 0x67, 1, 2, 3, 4, 9, 255}}).out.empty());
    assert(Poll(Collection(), {}).out.empty() && reads == 1);
    for (int fault = 0; fault < 8; ++fault) {
        auto c = Collection();
        if (fault == 0) ++c.vid;
        if (fault == 1) c.pid = 0xFFFF;
        if (fault == 2) ++c.caps.UsagePage;
        if (fault == 3) ++c.caps.Usage;
        if (fault == 4) ++c.caps.InputReportByteLength;
        if (fault == 5) ++c.caps.OutputReportByteLength;
        if (fault == 6) --c.caps.InputReportByteLength;
        if (fault == 7) --c.caps.OutputReportByteLength;
        assert(Poll(c, {Page(64)}).out.empty() && opens == 0);
    }
    openOk = false; assert(Poll(Collection(), {Page(64)}).out.empty() && writes == 0);
    openOk = true; writeOk = false; assert(Poll(Collection(), {}).out.empty() && reads == 0);
    puts("Raigor driver tests passed");
}
