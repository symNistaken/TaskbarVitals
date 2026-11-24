// ENDORFY Viro Wireless headset (3738:0072), LIV Plus Wireless mouse (Compx 3299:00A7)
// and Celeris 1800 keyboard (3738:00BB), all on their 2.4 GHz receivers.
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
constexpr USHORT kLivVid = 0x3299, kLivDongle = 0x00A7;
constexpr USHORT kCelerisDongle = 0x00BB;
constexpr BYTE kOutId = 0x06, kInId = 0x07;
constexpr size_t kReportLen = 62;
constexpr BYTE kRaceHead = 0x05, kRaceIndication = 0x5D;
constexpr USHORT kOpBattery = 0x0CD6;
constexpr int kReads = 12, kReadDelayMs = 60;  // the answer arrives within ~40 ms on other Airoha dongles

BYTE CompxChecksum(const BYTE* b) {
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) sum += b[i];
    return (BYTE)(0x55 - sum);
}

bool CompxQuery(HidDev& d, BYTE cmd, std::vector<BYTE>& r) {
    std::vector<BYTE> req(17, 0);
    req[0] = 0x08;
    req[1] = cmd;
    req[16] = CompxChecksum(req.data());
    d.Flush();
    if (!d.Write(req)) return false;
    ULONGLONG end = GetTickCount64() + 500;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || d.Read(r, left) < 17) return false;
        if (r[0] == 0x08 && r[1] == cmd && r[16] == CompxChecksum(r.data())) return true;
    }
}

// Celeris 1800's 2.4 GHz receiver uses the read-only EVision keyboard battery request.
// Report 04 echoes the 16-bit request id and command 1A; bytes 8 and 9 are level and charge state.
bool ReadCelerisBattery(HidDev& d, DeviceStatus& s) {
    static USHORT nextId = (USHORT)GetTickCount64();
    if (!nextId) nextId = 1;
    for (int attempt = 0; attempt < 2; ++attempt) {
        USHORT id = nextId++;
        if (!nextId) nextId = 1;
        std::vector<BYTE> req(64, 0);
        req[0] = 0x04;
        req[1] = (BYTE)id;
        req[2] = (BYTE)(id >> 8);
        req[3] = 0x1A;
        d.Flush();
        if (!d.Write(req)) continue;
        ULONGLONG end = GetTickCount64() + 500;
        std::vector<BYTE> r;
        while (true) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0 || d.Read(r, left) < 10) break;
            if (r[0] != 0x04 || r[1] != (BYTE)id || r[2] != (BYTE)(id >> 8) || r[3] != 0x1A) continue;
            if (r[4] || r[5] || r[6] || r[7] || r[8] > 100 || r[9] > 2) continue;
            s.percent = r[8];
            s.charging = r[9] != 0;
            return true;
        }
    }
    return false;
}

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
    // LIV Plus Wireless on its Compx 2.4 GHz receiver (the USB cable and Bluetooth use
    // different identities and are left to the generic HID / Windows battery paths).
    if (!ctx.IsClaimed(kLivVid, kLivDongle)) {
        for (const HidColl* c : ctx.Find(kLivVid, kLivDongle, [](const HidColl& c) {
                 return c.page() == 0xFF02 && c.usage() == 0x0002 && c.inLen() >= 17 && c.outLen() >= 17;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            DeviceStatus s;
            std::vector<BYTE> r;
            bool answered = CompxQuery(d, 0x03, r);
            if (answered) s.connected = r[6] != 0;
            if (!answered || s.connected) {
                if (CompxQuery(d, 0x04, r)) {
                    answered = true;
                    s.connected = true;
                    if (r[6] <= 100) s.percent = r[6];
                    s.charging = r[7] != 0;
                    s.millivolts = (r[8] << 8) | r[9];
                } else if (answered && s.connected) {
                    s.asleep = true;
                }
            }
            if (!answered) continue;
            if (!s.connected) { ctx.Claim(kLivVid, kLivDongle); break; }
            s.key = L"endorfy:liv-plus";
            s.name = L"ENDORFY LIV Plus Wireless";
            s.kind = DeviceKind::Mouse;
            s.via = L"2.4 GHz receiver";
            s.graceSec = 1800;
            ctx.Add(s, kLivVid, kLivDongle);
            break;
        }
    }

    // Celeris 1800 2.4 GHz receiver (3738:00BB), FF1C:0092 vendor collection.
    if (!ctx.IsClaimed(kVid, kCelerisDongle)) {
        for (const HidColl* c : ctx.Find(kVid, kCelerisDongle, [](const HidColl& c) {
                 return c.page() == 0xFF1C && c.usage() == 0x0092 && c.inLen() == 64 && c.outLen() == 64;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            DeviceStatus s;
            if (!ReadCelerisBattery(d, s)) continue;
            s.key = L"endorfy:celeris-1800";
            s.name = L"ENDORFY Celeris 1800";
            s.kind = DeviceKind::Keyboard;
            s.via = L"2.4 GHz receiver";
            s.graceSec = 1800;
            ctx.Add(s, kVid, kCelerisDongle);
            break;
        }
    }

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
