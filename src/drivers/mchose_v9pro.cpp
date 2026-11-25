// MCHOSE first-generation wireless headsets (V9 / V9 PRO / S9 PRO / G9 PRO / X9) on their 2.4 GHz
// dongle, VID 291D PID 385D (Sonix/CMedia "BLK" protocol; the dongle enumerates as a USB audio card
// with a vendor HID interface). The V9 PRO dongle answers on the FF90:0001 collection (65-byte
// reports, report id 0x55):
//   request (64 bytes with the report id): [55][65][01][00 x61]  (read-only battery query)
//   reply:   [55][65][level][state] with state 2 = discharging, 3 = charging, 4 = full,
//            26 (0x1A) = headset off/asleep; the headset also pushes the same frame shape
//            (report id 0x55, byte 1 = 0x65) as an unsolicited battery notification.
// Protocol from MCHOSE's own M HUB web driver (cdn.mchose.com.cn customPage/prod/assets
// main-DIt5vAcV.js: AUDIO_PROTOCOL_PROFILE.BLK, BLK_BATTERY_REPORT_ID = 85,
// BLK_BATTERY_COMMAND_ID = 101, audio.blk.readBattery, parseBatteryResponse), registered there for
// "MCHOSE V9 PRO--10525--14429" (10525 = 0x291D, 14429 = 0x385D). Confirmed on a real MCHOSE V9 PRO
// dongle: the FF90 collection answered 55 65 28 02 = 40%, discharging.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x291D;
constexpr USHORT kPid = 0x385D;
constexpr BYTE kReportId = 0x55;
constexpr BYTE kReadBattery = 0x65;
constexpr BYTE kStateDischarge = 2;
constexpr BYTE kStateCharging = 3;
constexpr BYTE kStateFull = 4;
constexpr BYTE kStateSleep = 26;

// Silent: the query went out but no battery frame came back within the deadline. Some dongles stop
// answering a while after the headset goes to sleep; without this the entry ran out of grace and
// vanished after 30 minutes although the dongle was still plugged in.
enum class Reply { None, Silent, Off, Level };

// One input report (r[0] = report ID) carrying the BLK battery frame, queried or pushed.
Reply ParseBattery(const std::vector<BYTE>& r, int& percent, bool& charging, bool& full) {
    if (r.size() < 4 || r[0] != kReportId || r[1] != kReadBattery) return Reply::None;
    const BYTE state = r[3];
    if (state == kStateSleep) return Reply::Off;
    percent = r[2];
    charging = state == kStateCharging;
    full = state == kStateFull;
    return percent <= 100 ? Reply::Level : Reply::None;
}

Reply QueryBattery(const HidColl& c, int& percent, bool& charging, bool& full) {
    HidDev d;
    if (!d.Open(c)) return Reply::None;
    d.Flush();
    if (!d.Write({kReportId, kReadBattery, 0x01})) return Reply::None;  // zero-padded to 64 bytes

    std::vector<BYTE> r;
    bool sawBatteryFrame = false;
    ULONGLONG end = GetTickCount64() + 1000;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 0) break;
        Reply got = ParseBattery(r, percent, charging, full);
        if (got != Reply::None) return got;
        if (r.size() >= 2 && r[0] == kReportId && r[1] == kReadBattery) sawBatteryFrame = true;
    }
    return sawBatteryFrame ? Reply::None : Reply::Silent;  // a malformed battery frame is not silence
}

}  // namespace

void PollMchoseV9Pro(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& c) {
             return c.page() == 0xFF90 && c.usage() == 0x0001 && c.outLen() >= 64 && c.inLen() >= 64;
         })) {
        int pct = -1;
        bool chg = false, full = false;
        Reply got = QueryBattery(*c, pct, chg, full);
        if (got == Reply::None) continue;

        DeviceStatus s;
        s.key = L"mchose:v9pro1";
        s.name = HidProduct(*c);
        if (s.name.empty()) s.name = L"MCHOSE V9 PRO";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz receiver";
        if (got == Reply::Off || got == Reply::Silent) {
            s.asleep = true;  // the headset is off or asleep (or the dongle went quiet): keep the last value
        } else {
            s.percent = pct;
            s.charging = chg;
            s.full = full;
        }
        s.graceSec = 1800;
        ctx.Add(s, kVid, kPid);
        break;
    }
}
