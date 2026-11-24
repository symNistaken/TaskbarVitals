// Quantum 350 (0ECB:206B): JBL QuantumENGINE 2.9.4 registers BatteryLevelProp (HIDV1 GET 49)
// and HIDV1WirelessConnectionStateProp (GET 68) for Q350TV1. Each returns one payload byte.
// The Windows collection is documented in https://github.com/krisgfx9/Wireless-Device-Battery-Monitor/issues/259
//
// Quantum 610 Wireless (0ECB:205C, ID from JBL's QuantumUSBAudio.inf as listed in
// https://github.com/sidmax7/QuantumEngine quantum-linux/crates/jbl-quantum/src/models.rs): the same HIDV1
// GET 49 (battery, 0-100) and GET 68 (link, 01 = headset connected) on HID interface 5, captured on a real
// 610 dongle in https://github.com/Tymon3310/jbl-quantum-linux/blob/main/docs/QUANTUM610_PACKETS.md.
// The firmware answers an unimplemented report id with the previous implemented one, so the reply's
// first byte must match the id asked for (GetFeatureByte checks it). Only GETs are sent.
//
// Quantum 360 Wireless (0ECB:2115, same ID source; dongle listed with the HIDV1 dongle models in
// https://github.com/Tymon3310/jbl-quantum-linux/blob/main/docs/QUANTUM_LINEUP.md). Not HIDV3: its
// Windows dump in https://github.com/krisgfx9/Wireless-Device-Battery-Monitor/issues/183 has no FF12
// collection, only the 350's layout of a Generic Desktop usage 0 collection (interface 5, 64-byte
// feature = the 63-byte HIDV1 reports) next to a telephony one. Same read-only GET 68 / GET 49.
//
// Quantum 810 Wireless (0ECB:2069 2.4 GHz dongle, HID product string "JBL Quantum810 Wireless Chat"):
// github.com/sidmax7/QuantumEngine (MIT / Apache-2.0), tested on this dongle, reads the battery with GET 49 and the
// link with GET 68 (quantum-linux/crates/jbl-quantum/src/protocol.rs), the same HIDV1 reports as the 610.
// A Windows `--dump` of a real 810 dongle (2026-11-20) shows a single HID collection on interface 5, FF13:0001
// (input 2, output 62, feature 64 bytes), and no FF12 collection; the level read there (75%) matched JBL
// QuantumENGINE.
//
// JBL Quantum 520 / 650 / 850 / 950 (and X variants) headsets, vendor 0ECB, "HIDV3" protocol on the
// vendor collection with usage page FF12 (64-byte reports: input 86, output 87, feature 88).
//   request (feature 88): [88][00 DD][01 00][01][00][02 00][feature lo][feature hi]
//            identifier DD00 = the device itself, command 0001 = get device info, 1 packet, index 0,
//            payload = the 2-byte feature id
//   reply   (input 86):   [86][00 DD][01 00][01][00][len lo][len hi][status lo][status hi]
//                         [feature lo][feature hi][size lo][size hi][value...]
//            status bit 15 set = error
// Battery = feature 000D, one byte: bit 7 charging, bits 0..6 percent.
// Read from JBL QuantumENGINE 2.9.4 (HIDV3Device / PowerFeature.Battery.HIDV3Left) and checked
// against a Quantum 650 Wireless on its 2.4 GHz dongle; the other models use the same code path there.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x0ECB;
constexpr USHORT kBatteryFeature = 0x000D;

struct Model { USHORT pid; const wchar_t* name; bool dongle; };
constexpr Model kModels[] = {
    { 0x2125, L"JBL Quantum 650 Wireless", true },  { 0x2126, L"JBL Quantum 650 Wireless", false },
    { 0x213B, L"JBL Quantum 650X Wireless", true }, { 0x213D, L"JBL Quantum 650X Wireless", false },
    { 0x2127, L"JBL Quantum 950 Wireless", true },  { 0x2128, L"JBL Quantum 950 Wireless", false },
    { 0x213E, L"JBL Quantum 950X Wireless", true }, { 0x2140, L"JBL Quantum 950X Wireless", false },
    { 0x218D, L"JBL Quantum 520 Wireless", true },  { 0x218E, L"JBL Quantum 520 Wireless", false },
    { 0x21A7, L"JBL Quantum 520X Wireless", true }, { 0x21A9, L"JBL Quantum 520X Wireless", false },
    { 0x219A, L"JBL Quantum 850 Wireless", true },  { 0x219B, L"JBL Quantum 850 Wireless", false },
};

// With the headset switched off the dongle does not answer at all, so a timeout means "not there".
bool GetBattery(HidDev& d, BYTE& info) {
    d.Flush();
    if (!d.SetFeature({ 0x88, 0x00, 0xDD, 0x01, 0x00, 0x01, 0x00, 0x02, 0x00, (BYTE)kBatteryFeature,
                        (BYTE)(kBatteryFeature >> 8) }))
        return false;
    ULONGLONG end = GetTickCount64() + 800;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 16) return false;
        // Only the reply to "get device info" sent to the device itself.
        if (r[0] != 0x86 || r[1] != 0x00 || r[2] != 0xDD || r[3] != 0x01 || r[4] != 0x00 || r[5] != 0x01) continue;
        if (r[10] & 0x80) return false;  // error status (bit 15)
        if (r[11] != (BYTE)kBatteryFeature || r[12] != (BYTE)(kBatteryFeature >> 8)) continue;
        if (r[13] == 0 && r[14] == 0) continue;  // empty feature value
        info = r[15];
        return (info & 0x7F) <= 100;
    }
    return false;
}

// HIDV1 headsets: GET 68 (wireless link) and GET 49 (battery percent), one payload byte each.
struct Hidv1Model { USHORT pid; int iface; USHORT page; const wchar_t* name; };  // page 0 = any collection
constexpr Hidv1Model kHidv1Models[] = {
    { 0x206B, 3, 0x0001, L"JBL Quantum 350 Wireless" },
    { 0x205C, 5, 0, L"JBL Quantum 610 Wireless" },
    { 0x2115, 5, 0x0001, L"JBL Quantum 360 Wireless" },
    { 0x2069, 5, 0, L"JBL Quantum 810 Wireless" },  // its interface 5 collection is FF13:0001
};

void PollHidv1(PollContext& ctx) {
    for (const Hidv1Model& m : kHidv1Models) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        for (const auto* c : ctx.Find(kVid, m.pid, [&m](const HidColl& c) {
                 return c.iface == m.iface && c.featLen() >= 2 &&
                        (m.page == 0 || (c.page() == m.page && c.usage() == 0));
             })) {
            HidDev d;
            BYTE linked = 0, percent = 0;
            if (!d.OpenReadOnly(*c) || !d.GetFeatureByte(0x68, linked) || linked != 1 ||
                !d.GetFeatureByte(0x49, percent) || percent > 100) continue;
            DeviceStatus s;
            s.key = L"jbl:" + Hex4(m.pid);
            s.name = m.name;
            s.kind = DeviceKind::Headset;
            s.via = L"2.4 GHz receiver";
            s.percent = percent;
            ctx.Add(s, kVid, m.pid);
            break;
        }
    }
}

}  // namespace

void PollJbl(PollContext& ctx) {
    PollHidv1(ctx);
    for (const Model& m : kModels) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, m.pid, [](const HidColl& c) {
                 return c.page() == 0xFF12 && c.featLen() >= 11 && c.inLen() >= 16;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            BYTE info = 0;
            if (!GetBattery(d, info)) continue;
            DeviceStatus s;
            s.key = L"jbl:" + Hex4(m.pid);
            s.name = m.name;
            s.kind = DeviceKind::Headset;
            s.via = m.dongle ? L"2.4 GHz receiver" : L"USB cable";
            s.percent = info & 0x7F;
            s.charging = (info & 0x80) != 0;
            ctx.Add(s, kVid, m.pid);
            break;
        }
    }
}
