// ENDORFY Viro Wireless headset on its 2.4 GHz dongle (3738:0072), issue #1398.
// No ENDORFY source documents this dongle. Its HID layout is the stock Airoha USB dongle one: a single
// vendor collection FF13:0001 on interface 3 with 62-byte input and output reports (report 06 out,
// report 07 in), next to the telephony and consumer collections. That is the layout the Audeze Maxwell,
// Skullcandy PLYR, Alienware AW920H and HyperX Cloud III dongles show, so it is read with Airoha's
// generic RACE "get battery" query, the one other Airoha 2.4 GHz dongles answer:
//  * HaloBattery (github.com/HeyOkay/HaloBattery, MIT): providers/skullcandy.py, taken from the Headroom
//    project (patalbansishashank/Headroom, bin/headroom_race.py, recovered from the Airoha SDK sources
//    Skull-HQ ships, confirmed on 34F0:5310 and on a Skull-HQ capture of 34F0:3210), and
//    providers/audeze.py (from HeadsetControl, lib/devices/audeze_maxwell.hpp), which sends the same packet.
//  * Request, output report 06, 62 bytes: 06 07 80 05 5A 03 00 D6 0C 00
//      [06][frame length][recipient 0x80 = the headset behind the dongle]
//      [RACE frame 05 5A <u16le len = 3> <u16le opcode 0x0CD6> <role 00 = agent>]
//  * Reply, input report 07 read with GetInputReport: [07][length][recipient] then RACE frames
//    05 <type> <u16le len> <u16le opcode> <payload>. The battery is the 0x5D indication of opcode 0x0CD6
//    with payload [status 0][role][percent]; it can follow the 0x5B acknowledgement in the same report.
//    There is no charging flag.
// The query only reads. If the headset is off, or the dongle does not answer this query, nothing is
// shown and the dongle is claimed so no other driver writes to it.
#include "drivers.h"

#include <algorithm>

namespace {

constexpr USHORT kVid = 0x3738, kViroDongle = 0x0072;
constexpr BYTE kOutId = 0x06, kInId = 0x07;
constexpr size_t kReportLen = 62;
constexpr BYTE kRaceHead = 0x05, kRaceIndication = 0x5D;
constexpr USHORT kOpBattery = 0x0CD6;
constexpr int kReads = 12, kReadDelayMs = 60;  // the answer arrives within ~40 ms on other Airoha dongles

bool IsVendorCollection(const HidColl& c) {
    return c.page() == 0xFF13 && c.usage() == 0x0001 && c.inLen() == kReportLen && c.outLen() == kReportLen;
}

// The percent carried by an input report 07, or -1 when it holds no battery indication.
int BatteryPercent(const std::vector<BYTE>& r) {
    if (r.size() < 9 || r[0] != kInId) return -1;
    size_t i = 3;
    while (i + 6 <= r.size()) {
        if (r[i] != kRaceHead) { i++; continue; }
        size_t len = r[i + 2] | (r[i + 3] << 8);
        if (len < 2 || i + 4 + len > r.size()) break;
        USHORT op = (USHORT)(r[i + 4] | (r[i + 5] << 8));
        if (r[i + 1] == kRaceIndication && op == kOpBattery && len >= 5 && r[i + 6] == 0 && r[i + 8] <= 100)
            return r[i + 8];
        i += 4 + len;
    }
    return -1;
}

int ReadLevel(const HidColl& c) {
    HidDev d;
    if (!d.Open(c)) return -1;
    // Report 07 still holds the previous answer, so a read identical to it is not taken as the new one
    // (except the last read, so a steady level is still shown).
    std::vector<BYTE> before;
    if (!d.GetInputReport(kInId, before)) before.clear();
    std::vector<BYTE> req(kReportLen, 0);
    const BYTE ask[] = { kOutId, 0x07, 0x80, kRaceHead, 0x5A, 0x03, 0x00, 0xD6, 0x0C, 0x00 };
    std::copy(std::begin(ask), std::end(ask), req.begin());
    if (!d.Write(req)) return -1;
    for (int i = 0; i < kReads; i++) {
        Sleep(kReadDelayMs);
        std::vector<BYTE> r;
        if (!d.GetInputReport(kInId, r)) continue;
        if (!before.empty() && r == before && i + 1 < kReads) continue;
        int level = BatteryPercent(r);
        if (level >= 0) return level;
    }
    return -1;
}

}  // namespace

void PollEndorfy(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kViroDongle)) return;
    for (const HidColl* c : ctx.Find(kVid, kViroDongle, IsVendorCollection)) {
        int level = ReadLevel(*c);
        ctx.Claim(kVid, kViroDongle);
        if (level < 0) return;  // headset off or no answer: leave it out, the tray keeps the last level for graceSec
        DeviceStatus s;
        s.key = L"endorfy:viro";
        s.name = L"ENDORFY Viro Wireless";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz dongle";
        s.percent = level;
        s.graceSec = 180;
        ctx.Add(s, kVid, kViroDongle);
        return;
    }
}
