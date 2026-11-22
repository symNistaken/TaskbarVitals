// NuPhy WH80 over USB (19F5:A011), hardware-tested.
// Source: https://drive.nuphy.io/static/js/main.b3fb29e0.js, NuPhyIO SDK
// getBatteryState(0): GetFunc (05), offset 0020, one byte. Only this GET is sent.
// Unnumbered 64-byte reports on usage 0001:0000, interface 1:
//   55 05 00 21 01 20 00 00 ... -> AA 05 00 checksum 01 20 00 00 value ...
// Checksum is the low byte of the sum of bytes 4..63. Value: bit 7 charging,
// bits 0..6 percentage (WH80 reports in 10% steps). Bluetooth/receiver untested.
#include "drivers.h"

namespace {
bool BatteryReply(const std::vector<BYTE>& r, int& percent, bool& charging) {
    if (r.size() != 64 || r[0] != 0xAA || r[1] != 0x05 || r[2] != 0 || r[4] != 1 ||
        r[5] != 0x20 || r[6] != 0 || r[7] != 0) return false;
    unsigned sum = 0;
    for (size_t i = 4; i < r.size(); ++i) sum += r[i];
    if ((sum & 0xFF) != r[3] || (r[8] & 0x7F) > 100) return false;
    percent = r[8] & 0x7F;
    charging = (r[8] & 0x80) != 0;
    return true;
}
}  // namespace

void PollNuphy(PollContext& ctx) {
    constexpr USHORT vid = 0x19F5, pid = 0xA011;
    if (ctx.IsClaimed(vid, pid)) return;
    for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
        return c.iface == 1 && c.page() == 1 && c.usage() == 0 && c.inLen() == 65 && c.outLen() == 65;
    })) {
        HidDev d;
        if (!d.Open(*c)) continue;
        d.Flush();
        if (!d.Write({ 0, 0x55, 0x05, 0, 0x21, 1, 0x20, 0, 0 }, 400)) continue;
        const ULONGLONG end = GetTickCount64() + 400;
        for (;;) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            std::vector<BYTE> r;
            if (left <= 0 || d.Read(r, left) < 0) break;
            DeviceStatus s;
            if (!BatteryReply(r, s.percent, s.charging)) continue;
            s.key = L"nuphy:WH80";
            s.name = L"NuPhy WH80";
            s.kind = DeviceKind::Keyboard;
            s.via = L"USB cable";
            ctx.Add(s, vid, pid);
            return;
        }
    }
}
