// Redragon wireless mice on an Evision 2.4 GHz receiver (320F:226F, "2.4G Wireless Receiver"), e.g. the K1NG M999.
// Same battery request as the ATK V75X driver (atk_keyboard.cpp, from HaloBattery / ATK HUB), which is Evision's
// protocol too. Confirmed on a Redragon M999 by a USB capture (Wireshark + USBPcap) of the vendor software:
//   Output report 04 on FF1C:0092, 31 payload bytes: [04][seq lo][seq hi][1A][02][00 ...]
//   Input report 04: [04][seq lo][seq hi][1A][02][00 00][00][percent][state]   (percent 0x61 = 97 % matched the vendor app)
// The state byte (0 on battery, 1/2 charging) comes from the ATK driver; on the M999 the charging indicator shows while it
// sits on its magnetic dock. With the USB cable plugged in the receiver kept answering 97 % with state 0 (the mouse probably
// switches to wired mode and stops talking to the receiver, which then repeats its last value).
// The vendor software repeats the request about every 300 ms; this app asks once per poll.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x320F, kReceiver = 0x226F;

bool ReadBattery(const HidColl& c, int& percent, bool& charging) {
    static USHORT seq = 0;
    seq = (USHORT)(seq % 0xFFFF + 1);
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    std::vector<BYTE> req(32, 0);
    req[0] = 0x04;
    req[1] = (BYTE)(seq & 0xFF);
    req[2] = (BYTE)(seq >> 8);
    req[3] = 0x1A;
    req[4] = 0x02;  // sent by the vendor software for this receiver
    if (!d.Write(req)) return false;
    ULONGLONG end = GetTickCount64() + 1000;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int n = d.Read(r, 100);
        if (n < 10 || r[0] != 0x04 || r[1] != (BYTE)(seq & 0xFF) || r[2] != (BYTE)(seq >> 8) || r[3] != 0x1A || r[5] || r[6])
            continue;
        if (r[8] > 100 || r[9] > 2) return false;
        percent = r[8];
        charging = r[9] == 1 || r[9] == 2;
        return true;
    }
    return false;
}

}  // namespace

void PollRedragonEvision(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kReceiver)) return;
    for (const HidColl* c : ctx.Find(kVid, kReceiver, [](const HidColl& c) { return c.page() == 0xFF1C && c.usage() == 0x0092; })) {
        DeviceStatus s;
        s.key = L"redragon_evision:" + Hex4(kReceiver);
        s.name = L"Redragon wireless mouse";  // the receiver id may be shared by several models; only the M999 is verified
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;
        int percent = -1;
        bool charging = false;
        if (ReadBattery(*c, percent, charging)) {
            s.percent = percent;
            s.charging = charging;
        } else {
            s.asleep = true;  // no reply: asleep, off or busy; keep the last value
        }
        ctx.Add(s, kVid, kReceiver);
        return;
    }
}
