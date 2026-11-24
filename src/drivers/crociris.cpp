// CROCIRIS X7 series wireless mice through their 8K receiver (36F9:AA20, "X7 Receiver 8K").
// CROCIRIS is XIBERIA's mouse brand; the X7 and X7 Ultra Max ship with this receiver.
//
// Protocol read from the vendor's own WebHID driver (hub.crociris.com, its index-*.js bundle: the
// "LE" codec with encodeMouseFrame/parseMouseFrame) and confirmed on hardware, showing 97% while
// the mouse was linked through its receiver:
//   vendor collection FF0A:0001, report id 0, 64 bytes of report data (65 with the leading id byte)
//   request: AA <cmd> <len> 00 <payload...>   zero padded to the report length
//   reply:   55 <cmd> <len> 00 <payload...>
//   cmd 0x12 = GetDeviceStatus, empty payload, read-only
//   reply payload[0] = charging (1 = yes), payload[1] = battery percent, payload[2] = full,
//   payload[3] & 0x0F = link mode (1 = the mouse is linked through this receiver)
// While the mouse sleeps the receiver keeps answering, but reports 0 for the level. A powered mouse
// is never at 0%, so 0 is treated as a missing reading: the last value is kept, dimmed, until a
// plausible level arrives. (Before this check the taskbar showed 0% for a mouse that still worked.)
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x36F9;
constexpr USHORT kReceiver = 0xAA20;
constexpr BYTE kRequestHeader = 0xAA;
constexpr BYTE kReplyHeader = 0x55;
constexpr BYTE kGetDeviceStatus = 0x12;
constexpr int kPollBudgetMs = 1000;

// HidDev::Read drops a leading 0 report id, so the reply starts at r[0] = 0x55 and payload[0] is r[4].
bool ReadBattery(const HidColl& c, int& percent, bool& charging, bool& full) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    std::vector<BYTE> req(c.outLen(), 0);
    req[0] = 0x00;  // unnumbered report: the buffer still starts with the id byte
    req[1] = kRequestHeader;
    req[2] = kGetDeviceStatus;
    req[3] = 0x00;  // payload length
    req[4] = 0x00;
    if (!d.Write(req)) return false;

    const ULONGLONG end = GetTickCount64() + kPollBudgetMs;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int n = d.Read(r, 100);
        if (n < 6 || r[0] != kReplyHeader || r[1] != kGetDeviceStatus) continue;
        const int len = r[2];
        if (len < 2 || n < 4 + len) continue;
        // Symptom: the taskbar showed 0% for a mouse that still worked. Cause: while the mouse
        // sleeps the receiver answers 0 for the level. A mouse in use is never at 0% (it would have
        // shut down already), so skip that reading and keep the last one, dimmed, instead of 0.
        if (r[4] > 1 || r[5] == 0 || r[5] > 100) continue;  // charging flag or level not plausible
        charging = r[4] == 1;
        percent = r[5];
        full = len > 2 && r[6] == 1;
        return true;
    }
    return false;
}

}  // namespace

void PollCrociris(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kReceiver)) return;
    // The mouse sleeps on its own schedule; keep the last reading for the next poll.
    static int lastPercent = -1;
    static bool lastCharging = false;

    for (const HidColl* c : ctx.Find(kVid, kReceiver, [](const HidColl& c) {
             return c.page() == 0xFF0A && c.usage() == 0x0001 && c.inLen() >= 6 && c.outLen() >= 5;
         })) {
        int percent = -1;
        bool charging = false;
        bool full = false;
        const bool answered = ReadBattery(*c, percent, charging, full);
        if (!answered) {
            percent = lastPercent;
            charging = lastCharging;
        } else if (percent >= 0) {
            lastPercent = percent;
            lastCharging = charging;
        }

        DeviceStatus s;
        s.key = L"crociris:x7";
        s.name = HidProduct(*c);
        if (s.name.empty()) s.name = L"CROCIRIS X7";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        s.charging = charging;
        s.full = full;
        s.asleep = !answered;
        s.graceSec = 1800;
        ctx.Add(s, kVid, kReceiver);
        return;
    }
}
