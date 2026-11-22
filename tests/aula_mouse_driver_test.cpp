#include "../src/drivers/drivers.h"
#include "../src/drivers/aula_mouse_protocol.h"
#include <cassert>
#include <cstdio>

namespace {
std::vector<std::vector<BYTE>> pending, writes;
std::vector<BYTE> battery;
bool noReply = false, openOk = true, down = false;
int opens = 0;
HidColl Collection() {
    HidColl c;
    c.vid = 0x320F; c.pid = 0x22A0; c.iface = 1;
    c.caps.UsagePage = 0xFF1C; c.caps.Usage = 0x92;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}
PollContext Poll(HidColl c = Collection()) {
    opens = 0; writes.clear(); pending.clear();
    PollContext ctx; ctx.colls = {c}; PollAulaMouse(ctx); return ctx;
}
}
bool HidDev::Open(const HidColl& c) { ++opens; c_ = c; return openOk; }
void HidDev::Close() {}
void HidDev::Flush() { pending.clear(); }
std::wstring HidProduct(const HidColl&) { return L"AULA-8K Dongle"; }
bool HidDev::Write(const std::vector<BYTE>& data, int timeout) {
    assert(timeout > 0 && timeout <= 150);
    assert(data.size() == 64 && data[0] == 4);
    assert(data[3] == 1 || data[3] == 2 || data[3] == 0x1A);
    writes.push_back(data);
    if (noReply) return true;
    if (data[3] == 0x1A) {
        // Short battery frames must not hide a complete reply queued after them.
        pending.push_back({4, 0, 0, 0x1A});
        pending.push_back({4, 0, 0, 0x1A, 0, 0, 0, 0});
        pending.push_back({4, 0, 0, 0x1A, 0, 0, 0, 0, 86});
        if (down) pending.push_back({4, 0xAA, 0, 0xAA, 0, 0, 0, 0, 0});
        pending.push_back(battery);
    } else pending.push_back(data);
    return true;
}
int HidDev::Read(std::vector<BYTE>& r, int timeout) {
    assert(timeout > 0 && timeout <= 100);
    if (pending.empty()) { Sleep(timeout); return -1; }
    r = pending.front(); pending.erase(pending.begin()); return (int)r.size();
}
int main() {
    battery = AulaMouseProtocol::Request(0x1A); battery[8] = 86;
    auto ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 86 && !ctx.out[0].charging);
    assert(ctx.IsClaimed(0x320F, 0x22A0) && ctx.out[0].kind == DeviceKind::Mouse);
    assert(writes.size() == 3 && writes[0][3] == 1 && writes[1][3] == 0x1A && writes[2][3] == 2);
    for (BYTE charge : {1, 2}) {
        battery[9] = charge; ctx = Poll(); assert(ctx.out[0].charging);
    }
    battery[9] = 0;
    for (BYTE level : {0, 101, 255}) {
        battery[8] = level; ctx = Poll(); assert(ctx.out[0].asleep && ctx.out[0].percent == -1);
    }
    battery[8] = 86; battery[9] = 3; ctx = Poll(); assert(ctx.out[0].asleep);
    battery[9] = 0; down = true; ctx = Poll(); assert(ctx.out[0].asleep && ctx.out[0].percent == -1);
    down = false;
    for (int i = 0; i < 6; ++i) {
        auto c = Collection();
        if (i == 0) ++c.vid;
        if (i == 1) ++c.pid;
        if (i == 2) ++c.iface;
        if (i == 3) ++c.caps.UsagePage;
        if (i == 4) ++c.caps.Usage;
        if (i == 5) c.caps.OutputReportByteLength = 32;
        ctx = Poll(c); assert(opens == 0 && writes.empty() && ctx.out.empty());
    }
    openOk = false; ctx = Poll(); assert(writes.empty() && ctx.out[0].asleep); openOk = true;
    noReply = true;
    auto start = GetTickCount64(); ctx = Poll();
    assert(GetTickCount64() - start < 1200 && ctx.out[0].asleep);
    assert(writes.size() == 2 && writes.back()[3] == 2);
    std::puts("AULA mouse driver: battery, charging, invalid replies, link down, matching, timeout OK");
}
