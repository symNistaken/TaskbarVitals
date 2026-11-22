// AULA A7Pro 2.4 GHz receiver (38A6:0001, interface 0, FF90:0001, 64-byte reports).
// AULA Audio Center 1.0.1.0 sends the read-only battery query 55 65 01, padded with zeros.
// Verified on hardware via interrupt I/O. Reply bytes 2/3 are battery percent and power state:
// 00 with the cable connected, 02 on battery. At 100% with the cable connected the charge light is
// off, so 00 identifies external power rather than active charging; report full at 100%.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x38A6, kPid = 0x0001;
constexpr BYTE kReport = 0x55, kBattery = 0x65;
constexpr int kReportLength = 64;

bool IsBatteryCollection(const HidColl& c) {
    return c.iface == 0 && c.page() == 0xFF90 && c.usage() == 0x0001 &&
           c.inLen() == kReportLength && c.outLen() == kReportLength && c.featLen() == 0;
}

bool ReadBattery(const HidColl& c, int& percent, bool& externalPower) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    const ULONGLONG deadline = GetTickCount64() + 750;
    if (!d.Write({kReport, kBattery, 0x01}, 200)) return false;

    std::vector<BYTE> r;
    for (ULONGLONG now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
        if (d.Read(r, static_cast<int>(deadline - now)) < 0) break;
        if (r.size() != kReportLength || r[0] != kReport || r[1] != kBattery || r[2] > 100 ||
            (r[3] != 0x00 && r[3] != 0x02))
            continue;
        percent = r[2];
        externalPower = r[3] == 0x00;
        return true;
    }
    return false;
}

}  // namespace

void PollAulaHeadset(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kPid, IsBatteryCollection)) {
        // AULA also uses this VID:PID and collection layout for the A700 (upstream issue #3436).
        if (HidProduct(*c) != L"AULA-A7Pro") continue;
        int percent = -1;
        bool externalPower = false;
        if (!ReadBattery(*c, percent, externalPower)) continue;

        DeviceStatus s;
        s.key = L"aula:a7pro";
        s.name = L"AULA A7Pro";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        s.charging = externalPower && percent < 100;
        s.full = externalPower && percent == 100;
        ctx.Add(s, kVid, kPid);
        break;
    }
}
