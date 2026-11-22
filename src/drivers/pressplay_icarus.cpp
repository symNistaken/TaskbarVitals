// Press Play ICARUS V2 Nearlink on its 2.4 GHz receiver (7768:6434).
// Protocol observed from Press Play's web hub (https://hub.pressplayid.com/) in USBPcap captures
// from this device at 100% and 90% battery. On vendor collection FFB2:0001 (64-byte input/output),
// send output report 06 05 followed by zeroes. The receiver answers on the input report with
// [0] = 06, [1] = 05, [2] = 26; [13] is the battery percent (0x64 at 100%, 0x5A at 90%).
// The other observed configuration commands are intentionally not sent.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x7768;
constexpr USHORT kPid = 0x6434;
constexpr USHORT kUsagePage = 0xFFB2;
constexpr USHORT kUsage = 0x0001;
constexpr size_t kReportLength = 64;
constexpr BYTE kReportId = 0x06;
constexpr BYTE kBatteryCommand = 0x05;
constexpr BYTE kReplyMarker = 0x26;
constexpr size_t kPercentOffset = 13;

bool QueryBattery(const HidColl& c, int& percent) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();

    std::vector<BYTE> request(c.outLen(), 0), reply;
    request[0] = kReportId;
    request[1] = kBatteryCommand;
    int n = d.SetOutputAndRead(request, reply, 500);
    if (n < (int)(kPercentOffset + 1) || reply.size() < kPercentOffset + 1 || reply[0] != kReportId ||
        reply[1] != kBatteryCommand || reply[2] != kReplyMarker || reply[kPercentOffset] > 100)
        return false;

    percent = reply[kPercentOffset];
    return true;
}

}  // namespace

void PollPressPlayIcarus(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& c) {
             return c.page() == kUsagePage && c.usage() == kUsage && c.inLen() == kReportLength &&
                    c.outLen() == kReportLength;
         })) {
        int percent = -1;
        if (!QueryBattery(*c, percent)) continue;

        DeviceStatus s;
        s.key = L"pressplay:icarus-v2";
        s.name = L"Press Play ICARUS V2 Nearlink";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.percent = percent;
        ctx.Add(s, kVid, kPid);
        break;
    }
}
