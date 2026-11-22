// Chimera V2 (VID 3554 / PID F512) wireless mouse.
// Battery protocol on the vendor collection FF02:0002 (17 in / 17 out): report id 0x08,
// checksum = 0x55 - sum(bytes 0..15).
//   cmd 0x03 -> online status : reply [08 03 00 00 00 status .. .. .. ..]  (byte[5] != 0 = online)
//   cmd 0x04 -> battery       : reply [08 04 00 00 00 status percent charging mv_hi mv_lo ..]
//     byte[5] == 0x02  : connected
//     byte[6]          : percent (1..100)
//     byte[7] != 0     : charging
//     bytes[8..9] big-endian : millivolts
// Read-only: only battery state is queried.
// Protocol discovered by probing the device (--dump shows 3554:F512 on FF02:0002 17/17/0).
#include "drivers.h"

#include <algorithm>

namespace {

// Sends a 17-byte checksummed report id 0x08 and waits for the matching reply with a valid
// checksum. Returns true when a valid reply is in `r`.
bool Query(HidDev& d, BYTE cmd, std::vector<BYTE>& r, int deadlineMs) {
    std::vector<BYTE> req(17, 0);
    req[0] = 0x08;
    req[1] = cmd;
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) sum += req[i];
    req[16] = (BYTE)(0x55 - sum);
    ULONGLONG end = GetTickCount64() + deadlineMs;
    d.Flush();
    for (;;) {
        LONGLONG left = (LONGLONG)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return false;
        if (!d.Write(req)) return false;
        int n = d.Read(r, (int)std::min<LONGLONG>(left, 800));
        if (n >= 17 && r[0] == 0x08 && r[1] == cmd) {
            unsigned rs = 0;
            for (int i = 0; i < 16; i++) rs += r[i];
            if (r[16] == (BYTE)(0x55 - rs)) return true;
        }
    }
}

}  // namespace

void PollChimera(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(0x3554)) {
        if (pid != 0xF512) continue;  // Chimera V2 only
        auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.page() == 0xFF02 && c.usage() == 0x0002 && c.inLen() >= 17 && c.outLen() >= 17;
        });
        for (const HidColl* c : colls) {
            HidDev d;
            if (!d.Open(*c)) continue;
            std::vector<BYTE> r;
            // Wake the device with the online-status query, then read the battery.
            if (!Query(d, 0x03, r, 800)) continue;
            if (r.size() < 7 || r[5] == 0) continue;  // not connected
            if (!Query(d, 0x04, r, 800)) continue;
            if (r.size() < 10 || r[5] != 0x02) continue;  // expect connected status
            int percent = r[6];
            if (percent < 1 || percent > 100) continue;
            DeviceStatus s;
            s.key = L"chimera:" + Hex4(vid) + L":" + Hex4(pid);
            s.name = L"Chimera V2";
            s.kind = DeviceKind::Mouse;
            s.via = L"2.4 GHz receiver";
            s.percent = percent;
            s.charging = r[7] != 0;
            s.millivolts = (r[8] << 8) | r[9];
            s.graceSec = 1800;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
