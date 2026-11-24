// Exercise the feature-report driver with a status reply captured from an L7 Ultra+ receiver.
#include "../src/drivers/drivers.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {
const std::vector<BYTE> l7UltraPlus = {
    0x12, 0xF9, 0xC8, 0xC7, 0xE9, 0xBF, 0xDD, 0xFA, 0xFA, 0xFF, 0xF6, 0xAE, 0xFF, 0xA9
};
std::vector<BYTE> reply;

PollContext Poll(USHORT vid, USHORT pid, std::vector<BYTE> status) {
    reply = status;
    reply.resize(65, 0xFF);
    HidColl c;
    c.vid = vid;
    c.pid = pid;
    c.caps.UsagePage = 0xFF01;
    c.caps.Usage = 1;
    c.caps.FeatureReportByteLength = 65;
    PollContext ctx;
    ctx.colls = { c };
    PollMchose(ctx);
    return ctx;
}
}  // namespace

bool HidDev::OpenFeatureOnly(const HidColl&) { return true; }
void HidDev::Close() {}
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    assert(data.size() == 65 && data[0] == 0x12 && data[1] == 0xF9);
    assert(std::all_of(data.begin() + 2, data.end(), [](BYTE b) { return b == 0xFF; }));
    return true;
}
bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    assert(reportId == 0x12);
    out = reply;
    return true;
}
std::wstring HidProduct(const HidColl&) { return L"MCHOSE L7 Ultra+"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}
int Clamp100(int v) { return std::clamp(v, 0, 100); }

int main() {
    auto ctx = Poll(0x5253, 0x1020, l7UltraPlus);
    assert(ctx.IsClaimed(0x5253, 0x1020) && ctx.out.size() == 1);
    const auto& mouse = ctx.out.front();
    assert(mouse.name == L"MCHOSE L7 Ultra+" && mouse.key == L"mchose:1020");
    assert(mouse.percent == 81 && !mouse.charging && !mouse.asleep);
    assert(mouse.via == L"2.4 GHz receiver" && mouse.kind == DeviceKind::Mouse);

    // A different paired model or USB product must not inherit the L7 Ultra+ exception.
    auto other = l7UltraPlus;
    other[4] ^= 1;
    assert(Poll(0x5253, 0x1020, other).out.empty());
    assert(Poll(0x5253, 0x1021, l7UltraPlus).out.empty());

    // Existing mice identify themselves with VID 5253; Realtek receivers can return 3837 or 41E4.
    assert(Poll(0x3837, 0x1001, other).out.size() == 1);
    other[2] = 0xE4 ^ 0xFF;
    other[3] = 0x41 ^ 0xFF;
    assert(Poll(0x3837, 0x100C, other).out.size() == 1);
    other[2] = 0x53 ^ 0xFF;
    other[3] = 0x52 ^ 0xFF;
    assert(Poll(0x5253, 0x1020, other).out.size() == 1);
    puts("MCHOSE feature-report driver tests passed");
}
