// VGN headsets on the "ZHOU" protocol (VID 372E). Currently supports the VGN OMNISOUND (ZHOUTING)
// headset on its 2.4 GHz dongle (372E:10BB) on the control collection 000C:0001 (32-byte input,
// 64-byte output, 16-byte feature reports; report ID 0x55). The wired variant and the Siren V2 use
// the same frame shape in V HUB but are not enabled here because they are untested.
//
// Protocol from VGN's own V HUB 4.0.8 renderer bundle (HdServe.getBattery, pid 4283 branch of
// ReadHidBuffer), verified on a ZHOUTING dongle:
//   Read battery: output report ID 0x55, payload [0] 0x65, [1] 0x01, rest zero (read-only).
//   The reply is an input report that carries report ID 0x55, then 0x65, then the percentage.
//   V HUB forces charging = false for this dongle; no charging bit is known, so charging stays false.
#include "drivers.h"

namespace {

constexpr USHORT kVgnVid = 0x372E;
constexpr USHORT kZhouWirelessPid = 0x10BB;
constexpr BYTE kReportId = 0x55;
constexpr BYTE kReadBattery = 0x65;

// Checks one input report for the battery reply. Reports are numbered here, so r[0] is the report
// ID; a reply that arrives unnumbered (first payload byte 0x65) is accepted as well.
bool ParseBattery(const std::vector<BYTE>& r, int& percent) {
    if (r.size() >= 3 && r[0] == kReportId && r[1] == kReadBattery) {
        percent = r[2];
    } else if (r.size() >= 2 && r[0] == kReadBattery) {
        percent = r[1];
    } else {
        return false;
    }
    return percent <= 100;
}

bool QueryZhou(const HidColl& c, int& percent) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    // Request: report ID 0x55, then 0x65 0x01. The collection has no interrupt OUT pipe, so the
    // request goes out as a HID output report and the reply is the next input report.
    std::vector<BYTE> cmd = { kReportId, kReadBattery, 0x01 };
    std::vector<BYTE> r;
    int n = d.SetOutputAndRead(cmd, r, 500);
    if (n < 0) return false;
    if (ParseBattery(r, percent)) return true;

    // Keep listening briefly: the battery frame can arrive behind other notifications.
    ULONGLONG end = GetTickCount64() + 300;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 0) break;
        if (ParseBattery(r, percent)) return true;
    }
    return false;
}

}  // namespace

void PollVgn(PollContext& ctx) {
    if (ctx.IsClaimed(kVgnVid, kZhouWirelessPid)) return;
    for (const HidColl* c : ctx.Find(kVgnVid, kZhouWirelessPid, [](const HidColl& c) {
             return c.page() == 0x000C && c.usage() == 0x0001 && c.outLen() >= 63 && c.inLen() >= 3;
         })) {
        int pct = -1;
        if (!QueryZhou(*c, pct)) continue;

        DeviceStatus s;
        s.key = L"vgn:zhou";
        s.name = HidProduct(*c);
        if (s.name.empty()) s.name = L"VGN OMNISOUND (ZHOUTING)";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz receiver";
        s.percent = Clamp100(pct);
        s.graceSec = 1800;  // the dongle answers while the headset is awake; keep the last value otherwise
        ctx.Add(s, kVgnVid, kZhouWirelessPid);
        break;
    }
}
