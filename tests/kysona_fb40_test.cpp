// Tests the production Kysona polling path with mocked HID reports.
// No USB device is opened; hardware testing of Aurora Pro was reported by the PR author.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <deque>
#include <cwchar>

namespace {
std::deque<std::vector<BYTE>> pending;
int opens = 0, writes = 0;
BYTE level = 73, online = 1, charging = 0;
bool respondBattery = true, badChecksum = false;

BYTE Checksum(const std::vector<BYTE>& b) {
    unsigned sum = 0;
    for (size_t i = 0; i < 16; ++i) sum += b[i];
    return (BYTE)(0x55 - sum);
}

HidColl Mouse(USHORT pid = 0xFB40) {
    HidColl c;
    c.vid = 0x3554;
    c.pid = pid;
    c.iface = 1;
    c.product = L"IO Aurora Pro 8K Receiver";
    c.caps.UsagePage = 0xFF02;
    c.caps.Usage = 0x0002;
    c.caps.InputReportByteLength = 17;
    c.caps.OutputReportByteLength = 17;
    return c;
}

HidColl Feature(USHORT pid = 0xFB40) {
    HidColl c;
    c.vid = 0x3554;
    c.pid = pid;
    c.iface = 1;
    c.caps.UsagePage = 0xFF04;
    c.caps.Usage = 0x0002;
    c.caps.FeatureReportByteLength = 8;
    return c;
}

PollContext Poll(std::vector<HidColl> colls) {
    opens = writes = 0;
    pending.clear();
    PollContext ctx;
    ctx.colls = std::move(colls);
    PollKysona(ctx);
    return ctx;
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    c_ = c;
    ++opens;
    return true;
}
void HidDev::Close() {}
void HidDev::Flush() { pending.clear(); }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    ++writes;
    assert(data.size() == 17 && data[0] == 0x08 && data[16] == Checksum(data));
    assert(data[1] == 0x03 || data[1] == 0x04);
    if (data[1] == 0x04 && !respondBattery) return true;

    std::vector<BYTE> reply(17, 0);
    reply[0] = 0x08;
    reply[1] = data[1];
    reply[6] = data[1] == 0x03 ? online : level;
    if (data[1] == 0x04) reply[7] = charging;
    reply[16] = Checksum(reply);
    if (badChecksum && data[1] == 0x04) reply[16] ^= 0xFF;
    pending.push_back(reply);
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 500);
    if (pending.empty()) return -1;
    out = pending.front();
    pending.pop_front();
    return (int)out.size();
}
std::wstring HidProduct(const HidColl& c) { return c.product; }
std::wstring Hex4(USHORT n) {
    wchar_t b[5];
    std::swprintf(b, 5, L"%04X", n);
    return b;
}
const wchar_t* GWolvesModelName(USHORT) { return L"G-Wolves mouse"; }
bool GWolvesIsCable(USHORT) { return false; }
// The actual receiver also has a keyboard collection for auxiliary controls.
DeviceKind KindFromCollections(const std::vector<HidColl>&, USHORT, USHORT) {
    return DeviceKind::Keyboard;
}

int main() {
    auto mouse = Mouse();
    auto feature = Feature();
    auto ctx = Poll({mouse, feature});
    assert(opens == 1 && writes == 2);
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x3554, 0xFB40));
    assert(ctx.out[0].name == L"IO Aurora Pro 8K");
    assert(ctx.out[0].key == L"kysona:3554:FB40");
    assert(ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.out[0].percent == 73 && !ctx.out[0].charging);

    charging = 1;
    level = 95;
    ctx = Poll({mouse, feature});
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 95 && ctx.out[0].charging);

    online = 0;
    ctx = Poll({mouse, feature});
    assert(ctx.out.empty() && ctx.IsClaimed(0x3554, 0xFB40) && writes == 1);
    online = 1;

    // Don't send any mouse command to the shared BY keyboard receiver layout.
    mouse.caps.InputReportByteLength = mouse.caps.OutputReportByteLength = 20;
    ctx = Poll({mouse, feature});
    assert(ctx.out.empty() && opens == 0 && writes == 0);
    mouse = Mouse();
    feature.caps.FeatureReportByteLength = 16;
    ctx = Poll({mouse, feature});
    assert(ctx.out.empty() && opens == 0);
    feature = Feature();
    ctx = Poll({mouse});
    assert(ctx.out.empty() && opens == 0);

    // FA09 stays excluded even if it exposes the same 17-byte collection.
    ctx = Poll({Mouse(0xFA09), Feature(0xFA09)});
    assert(ctx.out.empty() && opens == 0);

    // An invalid battery frame is never interpreted as a percentage.
    badChecksum = true;
    ctx = Poll({mouse, feature});
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].percent < 0);
    badChecksum = false;
    respondBattery = false;
    ctx = Poll({mouse, feature});
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].percent < 0);
    respondBattery = true;

    std::puts("Kysona IO FB40 receiver regression tests passed");
}
