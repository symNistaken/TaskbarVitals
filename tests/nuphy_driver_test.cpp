#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>

namespace {
std::vector<std::vector<BYTE>> replies;
std::vector<std::vector<BYTE>> writes;
int opens = 0;
bool openOk = true, writeOk = true;
HidColl Collection() {
    HidColl c;
    c.vid = 0x19F5; c.pid = 0xA011; c.iface = 1;
    c.caps.UsagePage = 1; c.caps.Usage = 0;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 65;
    return c;
}
std::vector<BYTE> Reply(BYTE battery) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0xAA; r[1] = 5; r[4] = 1; r[5] = 0x20; r[8] = battery;
    r[3] = (BYTE)(1 + 0x20 + battery);
    return r;
}
PollContext Poll(std::vector<std::vector<BYTE>> input, HidColl c = Collection()) {
    replies = std::move(input); writes.clear(); opens = 0;
    PollContext ctx; ctx.colls = { c }; PollNuphy(ctx); return ctx;
}
}
bool HidDev::Open(const HidColl&) { ++opens; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& r, int timeout) {
    assert(timeout == 400); writes.push_back(r); return writeOk;
}
int HidDev::Read(std::vector<BYTE>& r, int timeout) {
    assert(timeout > 0 && timeout <= 400);
    if (replies.empty()) return -1;
    r = replies.front(); replies.erase(replies.begin()); return (int)r.size();
}
int main() {
    for (BYTE level : { 0, 10, 50, 100, 0xE4 }) {
        auto ctx = Poll({ Reply(level) });
        assert(ctx.out.size() == 1 && ctx.out[0].percent == (level & 0x7F));
        assert(ctx.out[0].charging == ((level & 0x80) != 0));
        assert(ctx.out[0].kind == DeviceKind::Keyboard && ctx.IsClaimed(0x19F5, 0xA011));
        assert(writes == std::vector<std::vector<BYTE>>({ { 0, 0x55, 5, 0, 0x21, 1, 0x20, 0, 0 } }));
    }
    // Do not accept echoes, other registers/modes, errors, damaged or short replies.
    for (int index : { 0, 1, 2, 3, 4, 5, 6, 7 }) {
        auto bad = Reply(50); bad[index] ^= 1;
        assert(Poll({ bad }).out.empty());
        assert(Poll({ bad, Reply(50) }).out.size() == 1);
    }
    auto shortReply = Reply(50); shortReply.resize(8);
    assert(Poll({ shortReply }).out.empty());
    assert(Poll({ Reply(101) }).out.empty());
    assert(Poll({}).out.empty());
    for (int field = 0; field < 6; ++field) {
        auto c = Collection();
        if (field == 0) ++c.vid;
        if (field == 1) ++c.pid;
        if (field == 2) ++c.iface;
        if (field == 3) ++c.caps.Usage;
        if (field == 4) --c.caps.InputReportByteLength;
        if (field == 5) --c.caps.OutputReportByteLength;
        assert(Poll({ Reply(50) }, c).out.empty() && opens == 0);
    }
    openOk = false; assert(Poll({ Reply(50) }).out.empty() && writes.empty()); openOk = true;
    writeOk = false; assert(Poll({ Reply(50) }).out.empty()); writeOk = true;
    PollContext claimed; claimed.colls = { Collection() }; claimed.Claim(0x19F5, 0xA011);
    opens = 0; PollNuphy(claimed); assert(opens == 0);
    std::puts("nuphy_driver_test: ok");
}
