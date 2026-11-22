// AKG N9 Hybrid on its USB-C dongle (0ECB:20D9, Harman vendor id).
// The dongle is an Airoha part and speaks Airoha's RACE protocol on the vendor collection FF13:0001
// (interface 3), wrapped the same way as the Audeze Maxwell (see audeze.cpp):
//   request:  62-byte output report 06: [06][07][80][05 5A 03 00 D6 0C 00]
//             = RACE command (05 5A), length 3, id 0x0CD6 (battery), agent 00
//   answer:   62-byte input report 07, fetched with GetInputReport: [07][valid length][80] followed by recent
//             RACE frames; the battery reply is [05 5D 05 00 D6 0C][status 00][agent][percent].
//             With the headset off there is no battery reply in the valid part (only notifications 2C80 / 2CB1).
// Checked on an AKG N9 Hybrid playing music on the dongle: 05 5D 05 00 D6 0C 00 00 32 = 50 %, the level the
// AKG Headphones app showed at the same time. The dongle also acknowledges with 05 5B ... D6 0C 00 and keeps
// sending other RACE notifications (ids 0901, 2C80, 2C82, 2CB1) that are not needed here.
// Only this single read-only query is sent. No charging flag is known.
#include "drivers.h"

#include <algorithm>

namespace {

constexpr USHORT kVid = 0x0ECB, kN9HybridDongle = 0x20D9;

// The newest battery reply in a report 07 buffer, or -1. Byte 1 is the number of valid bytes from byte 3 on;
// the rest of the report is left over from earlier answers. With the headset off the dongle stops answering
// the query, but an old battery frame can still sit in that leftover tail, so it must not be read.
int FindLevel(const std::vector<BYTE>& b) {
    if (b.size() < 4) return -1;
    size_t end = (std::min)(b.size(), (size_t)3 + b[1]);
    int level = -1;
    for (size_t i = 3; i + 8 < end; i++) {
        if (b[i] != 0x05 || b[i + 1] != 0x5D || b[i + 2] != 0x05 || b[i + 3] != 0x00) continue;
        if (b[i + 4] != 0xD6 || b[i + 5] != 0x0C || b[i + 6] != 0x00) continue;  // id 0x0CD6, status ok
        if (b[i + 8] <= 100) level = b[i + 8];
    }
    return level;
}

int ReadLevel(const HidColl& c) {
    HidDev d;
    if (!d.Open(c)) return -1;
    std::vector<BYTE> req(c.outLen(), 0);
    const BYTE q[] = { 0x06, 0x07, 0x80, 0x05, 0x5A, 0x03, 0x00, 0xD6, 0x0C, 0x00 };
    std::copy(std::begin(q), std::end(q), req.begin());
    if (!d.Write(req)) return -1;
    int level = -1;
    ULONGLONG end = GetTickCount64() + 600;  // the reply shows up within ~100 ms while the headset is on
    while (GetTickCount64() < end) {
        Sleep(15);
        std::vector<BYTE> r;
        if (!d.GetInputReport(0x07, r)) continue;
        int l = FindLevel(r);
        if (l >= 0) level = l;
        if (level >= 0) break;
    }
    return level;
}

}  // namespace

void PollAkg(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kN9HybridDongle)) return;
    for (const HidColl* c : ctx.Find(kVid, kN9HybridDongle, [](const HidColl& c) {
             return c.iface == 3 && c.page() == 0xFF13 && c.usage() == 0x0001 && c.inLen() == 62 && c.outLen() == 62;
         })) {
        int level = ReadLevel(*c);
        if (level < 0) continue;
        DeviceStatus s;
        s.key = L"akg:" + Hex4(kN9HybridDongle);
        s.name = L"AKG N9 Hybrid";
        s.kind = DeviceKind::Headset;
        s.via = L"USB dongle";
        s.percent = level;
        ctx.Add(s, kVid, kN9HybridDongle);
        break;
    }
}
