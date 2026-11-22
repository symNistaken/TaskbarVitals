// AJAZZ AK992 on the Telink 2.4 GHz receiver. EVision's read-only battery query is documented in
// https://github.com/HeyOkay/HaloBattery/blob/main/docs/protocols.md and verified on this receiver.
// Numbered report 04 echoes the little-endian request ID and command 1A, then reports the
// percentage and charging state at bytes 8 and 9 (including the report ID).
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x320F;
constexpr USHORT kPid = 0x50C3;
constexpr BYTE kReport = 0x04;
constexpr BYTE kBatteryCommand = 0x1A;
constexpr int kReportSize = 64;

bool ReadBattery(HidDev& device, DeviceStatus& status) {
    const auto sequence = static_cast<USHORT>(GetTickCount64());
    const auto low = static_cast<BYTE>(sequence);
    const auto high = static_cast<BYTE>(sequence >> 8);
    device.Flush();
    if (!device.Write({ kReport, low, high, kBatteryCommand }, 300)) return false;

    const ULONGLONG deadline = GetTickCount64() + 600;
    std::vector<BYTE> reply;
    for (ULONGLONG now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
        const int count = device.Read(reply, static_cast<int>(deadline - now));
        if (count < 0) return false;
        if (count < 10 || reply[0] != kReport || reply[1] != low || reply[2] != high ||
            reply[3] != kBatteryCommand || reply[5] != 0 || reply[6] != 0 || reply[8] > 100 || reply[9] > 2)
            continue;
        status.percent = reply[8];
        status.charging = reply[9] != 0;
        return true;
    }
    return false;
}

}  // namespace

void PollAjazzAk992(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* collection : ctx.Find(kVid, kPid, [](const HidColl& c) {
             return c.iface == 1 && c.page() == 0xFF1C && c.usage() == 0x0092 && c.inLen() == kReportSize &&
                    c.outLen() == kReportSize && c.featLen() == 0;
         })) {
        HidDev device;
        if (!device.Open(*collection)) continue;
        DeviceStatus status;
        if (!ReadBattery(device, status)) continue;
        status.key = L"ajazz-ak992:320F:50C3";
        status.name = L"AJAZZ AK992";
        status.kind = DeviceKind::Keyboard;
        status.via = L"2.4 GHz receiver";
        status.graceSec = 1800;
        ctx.Add(status, kVid, kPid);
        return;
    }
}
