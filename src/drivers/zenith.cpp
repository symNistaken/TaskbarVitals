// Zenith Pro mouse (260D:1114, "Segment" vendor software). Observed on hardware: write a 17-byte
// report on the FF02:0002 collection, [08][04][0 x 14][checksum = 0x55 - sum(bytes 0..15)];
// the mouse answers 17 bytes: [08][04]...[6=percent][7=charging][8..9=millivolts big-endian]
// [16=checksum, byte sum of the whole report is 0x55 mod 256].
#include "drivers.h"

static void PollZenithPid(PollContext& ctx, USHORT pid, const wchar_t* via) {
    const USHORT vid = 0x260D;
    if (ctx.IsClaimed(vid, pid)) return;
    auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
        return c.iface == 1 && c.page() == 0xFF02 && c.usage() == 2 && c.inLen() == 17 && c.outLen() == 17;
    });
    if (colls.size() != 1) return;

    HidDev dev;
    if (!dev.Open(*colls[0])) return;
    std::vector<BYTE> req(17, 0), r;
    req[0] = 0x08;
    req[1] = 0x04;
    req[16] = 0x49;
    dev.Flush();
    if (!dev.Write(req)) return;
    ULONGLONG end = GetTickCount64() + 1000;
    for (int reports = 0; reports < 32; reports++) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || dev.Read(r, left) < 0) return;
        if (r.size() != 17 || r[0] != 0x08 || r[1] != 0x04 || r[6] > 100) continue;
        BYTE sum = 0;
        for (BYTE b : r) sum += b;
        if (sum != 0x55) continue;
        DeviceStatus s;
        s.key = L"zenith:" + Hex4(vid) + L":" + Hex4(pid);
        s.name = L"Zenith Pro";
        s.kind = DeviceKind::Mouse;
        s.via = via;
        s.percent = r[6];
        s.charging = r[7] != 0;
        s.millivolts = (r[8] << 8) | r[9];
        s.graceSec = 1800;  // the dongle stops answering while the mouse sleeps: keep the last value
        ctx.Add(s, vid, pid);
        return;
    }
}

void PollZenith(PollContext& ctx) {
    PollZenithPid(ctx, 0x1114, L"2.4 GHz receiver");
    PollZenithPid(ctx, 0x1143, L"USB cable");
}
