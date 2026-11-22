// Fantech Raigor III, Raigor II WG10 and other cheap 2.4 GHz Mouse devices on the reused Sharkoon
// 1EA7:0066 / 0064 dongle. Owners of both Raigor models reported the same FFB5:0001 collection
// and ids in krisgfx9/Wireless-Device-Battery-Monitor issues #2891, #411 and #1312.
// Battery lives in the vendor collection FFB5:0001, report id B5, 8-byte in/out reports:
//   output [B5 05 00 00 00 00 00 00] -> the dongle streams its 256-byte status page as 64 frames
//          [B5 05 addr 04 d0 d1 d2 d3], addr = 00, 04, ... FC; chunk 94 holds [id3, state, %].
//   the dongle also pushes [B5 67 id[4] state level] on wake / re-link / heartbeat.
// The page answers even while the mouse sleeps but holds the last radio report, so a sleeping
// mouse keeps its last value (graceSec). This firmware does not report charging at all.
// Protocol: github.com/tuttizpro218/t20-battery (docs/PROTOCOL.md), verified live on a Fantech
// Raigor III dongle 1EA7:0066 (chunk 94 = 35 09 40 38 -> 64%).
#include "drivers.h"

#include <algorithm>

namespace {

constexpr USHORT kVid = 0x1EA7;

bool QueryBattery(HidDev &d, int &pct)
{
    d.Flush();
    if (!d.Write({ 0xB5, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 })) return false;

    std::vector<BYTE> r;
    ULONGLONG end = GetTickCount64() + 1200;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) break;
        int n = d.Read(r, std::min(left, 200));
        if (n < 0) break;
        if (n != 8 || r.size() != 8) continue;
        if (r[0] != 0xB5) continue;
        if (r[1] == 0x05 && r[2] == 0x94 && r[3] == 0x04) {  // status page chunk 0x94
            if (r[6] <= 100) { pct = r[6]; return true; }
        }
        if (r[1] == 0x67 && r[7] <= 100) {  // wake / heartbeat announcement
            pct = r[7];
            return true;
        }
    }
    return false;
}

}  // namespace

void PollRaigor(PollContext &ctx)
{
    for (auto [vid, pid] : ctx.Products(kVid)) {
        if (pid != 0x0066 && pid != 0x0064) continue;
        for (const HidColl *c : ctx.Find(vid, pid, [](const HidColl &c) {
                 return c.page() == 0xFFB5 && c.usage() == 0x0001 && c.inLen() == 8 && c.outLen() == 8;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            int pct = -1;
            if (!QueryBattery(d, pct)) continue;

            DeviceStatus s;
            s.key = L"raigor:" + Hex4(vid) + L":" + Hex4(pid);
            s.name = HidProduct(*c);
            if (s.name.empty()) s.name = L"2.4 GHz mouse";
            s.kind = KindFromCollections(ctx.colls, vid, pid);
            if (s.kind == DeviceKind::Other) s.kind = DeviceKind::Mouse;
            s.via = L"2.4 GHz receiver";
            s.percent = pct;
            s.graceSec = 1800;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
