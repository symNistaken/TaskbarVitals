// PlayStation PULSE Explore earbuds on the PlayStation Link USB adapter (054C:0ECC).
// Read-only: the same feature reports the PlayStation Link PC app polls, on the vendor collection
// with usage page FF01, usage 0020 (64-byte feature reports, interface 3):
//   82: link state, byte 1 = 01 while a device is connected, 00 otherwise
//   A0: name of the connected device, ASCII from byte 1 ("PULSE Explore")
//   B0: device status; fails while nothing is connected. Byte 55 holds the earbud levels as two
//       nibbles on a 0-15 scale: low nibble = left, high nibble = right.
// Decoded from USB captures of the PlayStation Link app (version on Windows, November 2026) and
// checked on one pair of PULSE Explore earbuds: AE = L 93% R 67%, FF = L 100% R 100%, matching
// what the app showed. Charging state, the case level and buds-in-case readings are not decoded.
// The PULSE Elite headset uses the same adapter, but its status layout is unverified, so other
// device names are left alone.
#include "drivers.h"
#include <algorithm>

namespace {

constexpr USHORT kVid = 0x054C, kPid = 0x0ECC;
constexpr size_t kLevelsByte = 55;

int NibbleToPercent(int n) { return (n * 100 + 7) / 15; }  // rounded: 14 -> 93, 10 -> 67

std::wstring AsciiName(const std::vector<BYTE>& r) {
    std::wstring s;
    for (size_t i = 1; i < r.size() && r[i] >= 0x20 && r[i] < 0x7F; i++) s += (wchar_t)r[i];
    return s;
}

}  // namespace

void PollPlayStationLink(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    auto filter = [](const HidColl& c) { return c.page() == 0xFF01 && c.usage() == 0x0020 && c.featLen() >= 64; };
    for (const HidColl* c : ctx.Find(kVid, kPid, filter)) {
        HidDev d;
        if (!d.OpenFeatureOnly(*c)) continue;
        DeviceStatus s;
        s.key = L"pslink:" + Hex4(kPid);
        s.name = L"PULSE Explore";
        s.kind = DeviceKind::Headset;
        s.via = L"PlayStation Link adapter";

        std::vector<BYTE> link, name, status;
        if (!d.GetFeature(0x82, link) || link.size() < 2) continue;
        if (link[1] == 0x00 || !d.GetFeature(0xA0, name) || !d.GetFeature(0xB0, status) ||
            status.size() <= kLevelsByte || status[0] != 0xB0) {
            s.connected = false;  // adapter present, earbuds off or in the case: hidden
            ctx.Add(s, kVid, kPid);
            return;
        }
        if (AsciiName(name) != L"PULSE Explore") return;  // another PlayStation Link device: layout unknown

        BYTE levels = status[kLevelsByte];
        int left = NibbleToPercent(levels & 0x0F);
        int right = NibbleToPercent(levels >> 4);
        s.percent = std::min(left, right);
        s.detail = L"L " + std::to_wstring(left) + L"%, R " + std::to_wstring(right) + L"%";
        ctx.Add(s, kVid, kPid);
        return;
    }
}
