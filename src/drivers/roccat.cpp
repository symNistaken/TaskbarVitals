// ROCCAT Syn Max Air headset (1E7D:3A3F) on its 2.4 GHz receiver, a Turtle Beach "Stellar Wireless" platform
// sold under the ROCCAT brand (the Stealth 600 / 700 Gen 2 family uses the same receiver layout).
// Protocol captured with USBPcap from ROCCAT Swarm (firmware 1.08) and first implemented in the Rust port
// (github.com/ferloom/Wireless-Device-Battery-Monitor, src/drivers/roccat.rs, GPL-3.0; same author).
//   The receiver's vendor collection FF00:0001 (interface 0) has 64-byte unnumbered input and output reports and
//   relays one request to the headset, the same request Swarm sends every 6 s:
//     request  8A 07 06 01
//     reply    8A 83 06 02 LL 00 V1 V1 V2 V2 CC
//              LL     coarse level 0..5 (what Swarm displays; 6 while charging)
//              V1 V1  cell voltage in mV, little-endian, 20 mV resolution (50 0F = 3920 mV)
//              V2 V2  second (filtered) voltage
//              CC     01 while charging / on the dock
//   Byte 0 is a tag the headset echoes, so the reply can be told from the unsolicited E0..E6 event reports
//   (mic arm, wheels) the headset pushes on the same collection. No reply within 1.5 s: headset switched off or
//   asleep while the receiver stays enumerated.
// Percent comes from a voltage curve measured on one unit over a full discharge under typical load (audio and
// RGB on): 14.5 h from 4080 mV to 3660 mV, about 16 h in total, which matches the manufacturer's rating. While
// charging the terminal voltage jumps by 200-300 mV within minutes and says little about the state of charge, so
// only the device's own level is shown then. Nothing on the headset is changed; the request is read-only.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x1E7D;
constexpr USHORT kSynMaxAir = 0x3A3F;

// Discharging under load: voltage (mV) -> percent, measured on hardware.
struct Point { int mv, pct; };
const Point kDischarge[] = { { 4080, 100 }, { 4040, 97 }, { 4000, 92 }, { 3960, 86 }, { 3920, 78 }, { 3900, 74 },
                             { 3880, 70 },  { 3840, 66 }, { 3820, 63 }, { 3800, 60 }, { 3780, 53 }, { 3760, 45 },
                             { 3740, 34 },  { 3720, 26 }, { 3700, 20 }, { 3680, 15 }, { 3660, 11 }, { 3640, 7 },
                             { 3600, 3 },   { 3500, 0 } };

}  // namespace

int RoccatSynPercentFromMillivolts(int mv) {
    const size_t n = sizeof(kDischarge) / sizeof(kDischarge[0]);
    if (mv >= kDischarge[0].mv) return kDischarge[0].pct;
    if (mv <= kDischarge[n - 1].mv) return kDischarge[n - 1].pct;
    for (size_t i = 0; i + 1 < n; i++) {
        const Point& a = kDischarge[i];
        const Point& b = kDischarge[i + 1];
        if (mv <= a.mv && mv >= b.mv) return b.pct + (mv - b.mv) * (a.pct - b.pct) / (a.mv - b.mv);
    }
    return 0;
}

// Decodes the 8A 83 06 02 ... status reply (hidapi-style buffer, r[0] == 0x8A). False when it is not one.
bool RoccatSynDecode(const std::vector<BYTE>& r, DeviceStatus& s) {
    if (r.size() < 10 || r[0] != 0x8A || r[1] != 0x83 || r[2] != 0x06) return false;
    const int level = r[4];
    const int mv = r[6] | (r[7] << 8);
    const bool charging = r.size() > 10 && r[10] == 0x01;
    s.kind = DeviceKind::Headset;
    s.millivolts = mv;
    s.charging = charging;
    s.connected = true;
    if (charging) {
        // Terminal voltage says little while charging: show the device's own coarse level.
        s.percent = -1;
        s.full = mv >= 4180;
        s.levelText = s.full ? L"Full" : L"Charging";
        s.detail = std::to_wstring(mv) + L" mV, level " + std::to_wstring(level < 5 ? level : 5) + L"/5";
    } else {
        s.percent = RoccatSynPercentFromMillivolts(mv);
        s.detail = std::to_wstring(mv) + L" mV, level " + std::to_wstring(level < 5 ? level : 5) + L"/5";
    }
    return true;
}

namespace {

// Sends the status request and waits for the reply with our tag, skipping the headset's event reports.
bool Query(HidDev& d, std::vector<BYTE>& reply) {
    d.Flush();
    if (!d.Write({ 0x00, 0x8A, 0x07, 0x06, 0x01 })) return false;
    const ULONGLONG deadline = GetTickCount64() + 1500;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) return false;
        std::vector<BYTE> r;
        if (d.Read(r, (int)(deadline - now)) <= 0) return false;
        if (!r.empty() && r[0] == 0x8A) {
            reply = r;
            return true;
        }
    }
}

}  // namespace

void PollRoccat(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kSynMaxAir)) return;
    auto colls = ctx.Find(kVid, kSynMaxAir, [](const HidColl& c) {
        return c.page() == 0xFF00 && c.usage() == 0x0001 && c.inLen() >= 64 && c.outLen() >= 64;
    });
    if (colls.empty()) return;
    DeviceStatus s;
    s.key = L"roccat:" + Hex4(kSynMaxAir);
    s.name = L"ROCCAT Syn Max Air";
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";
    s.graceSec = 60;
    HidDev d;
    std::vector<BYTE> reply;
    if (d.Open(*colls[0]) && Query(d, reply) && RoccatSynDecode(reply, s)) {
        ctx.Add(s, kVid, kSynMaxAir);
        return;
    }
    // Receiver present, headset switched off or asleep (or Swarm holding the collection): hidden until it answers.
    s.connected = false;
    ctx.Add(s, kVid, kSynMaxAir);
}
