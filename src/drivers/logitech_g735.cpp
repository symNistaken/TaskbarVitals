// Logitech G735 headset on its LIGHTSPEED dongle (046D:0AD8). The dongle has no classic HID++
// collections (no 7/20-byte FF00/FF43 reports); the headset is reached through the vendor collection
// FF03:0001, report F2 (271 bytes in and out), which wraps Centurion-style HID++ 2.0 messages:
//   request  F2 01 05 1E FA 82 40 CE 00 00 00 20 00 00 00 00 | idx  fn<<4|sw  params...
//   reply    F2 00 00 0C 1E 82 40 00 00 F2 00 len 00 00 00 00 | idx  fn<<4|sw  data...
//   error    ...                                              | FF   idx       fn<<4|sw  code
// Root (index 0) getFeature(0x0104 CenturionBatterySoc) gives the battery feature index (2 on the
// tested unit, version 3); its function 0 returns [level %][next level %][charging state], charging
// state 1 or 2 = charging (same layout as HeadsetControl's Centurion parser). Error 5 = no such feature.
// Source: USB-level capture of G HUB 2026.11 talking to the headset (G HUB showed 40 %, the reply
// said 41), then reproduced with these two read-only requests only. Our software id 0xB keeps
// replies apart from G HUB (0xD) and other apps (0xF). Both requests share one deadline.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x046D;
constexpr USHORT kPid = 0x0AD8;
constexpr BYTE kReport = 0xF2;
constexpr BYTE kSwId = 0x0B;
constexpr size_t kPayload = 16;  // offset of the HID++ message inside the report
constexpr int kDeadlineMs = 1000;
const BYTE kHeader[kPayload] = { 0xF2, 0x01, 0x05, 0x1E, 0xFA, 0x82, 0x40, 0xCE, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00 };

// 1 = reply (data after idx and fn|sw in `data`), 0 = HID++ error, -1 = no answer before `end`.
int Call(HidDev& d, ULONGLONG end, BYTE idx, BYTE fn, std::initializer_list<BYTE> params, std::vector<BYTE>& data) {
    std::vector<BYTE> req(kHeader, kHeader + kPayload);
    const BYTE fnsw = (BYTE)((fn << 4) | kSwId);
    req.push_back(idx);
    req.push_back(fnsw);
    req.insert(req.end(), params.begin(), params.end());
    d.Flush();
    if (!d.Write(req, 250)) return -1;
    std::vector<BYTE> r;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return -1;
        int n = d.Read(r, left);
        if (n < 0) return -1;
        if (n < (int)kPayload + 4 || r[0] != kReport || r[1] != 0x00) continue;
        const BYTE* m = r.data() + kPayload;
        if (m[0] == idx && m[1] == fnsw) {
            data.assign(r.begin() + kPayload + 2, r.begin() + n);
            return 1;
        }
        if (m[0] == 0xFF && m[1] == idx && m[2] == fnsw) return 0;
    }
}

}  // namespace

void PollLogitechG735(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    auto colls = ctx.Find(kVid, kPid, [](const HidColl& c) {
        return c.page() == 0xFF03 && c.usage() == 0x0001 && c.inLen() == 271 && c.outLen() == 271;
    });
    if (colls.empty()) return;
    HidDev d;
    if (!d.Open(*colls[0])) return;

    DeviceStatus s;
    s.key = L"g735:" + Hex4(kPid);
    s.name = L"Logitech G735";
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";

    const ULONGLONG end = GetTickCount64() + kDeadlineMs;
    std::vector<BYTE> p;
    // Root getFeature(0x0104); the dongle answers for an absent headset with an error or not at all.
    BYTE idx = (Call(d, end, 0x00, 0, { 0x01, 0x04 }, p) == 1 && !p.empty()) ? p[0] : 0;
    if (idx && Call(d, end, idx, 0, {}, p) == 1 && !p.empty() && p[0] <= 100) {
        s.percent = p[0];
        s.charging = p.size() >= 3 && (p[2] == 1 || p[2] == 2);
    } else {
        s.connected = false;  // headset off or out of range
    }
    ctx.Add(s, kVid, kPid);
}
