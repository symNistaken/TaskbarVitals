// MCHOSE first-generation A5 mice (VID 2023): A5 Pro and A5 Pro Max. These predate M HUB and use
// a different chip and protocol from the 5253 / 3837 families in mchose.cpp:
//   2023:F013  1K 2.4 GHz receiver (shared by A5 Pro and A5 Pro Max; product string "MCHOSE A5 2.4G")
//   2023:F015  4K 2.4 GHz receiver (shared as well)
//   2023:F017  A5 Pro on its USB cable
//   2023:F018  A5 Pro Max 1K receiver (Klegus/mchose-macos table)
//   2023:F019  A5 Pro Max on its USB cable
// Transport: 64-byte feature reports (65 with the report id) on the vendor collection, usage page
// FFFF (interface 2), report id 0. Only the battery read is sent:
//   request: [00 report id][00 00][route 02 = mouse][len 02][page 00][cmd 83][00 ...]
//   reply:   read back with a get-feature on report id 0:
//            [00 report id][A1 success][..][..][..][page 00][cmd 83][charging][level]
//            (0xA1 = OK, 0xA3 = unsupported; page and command are echoed)
// The mouse only answers while it is awake, so the reference writes once, waits 34 ms, then reads
// back up to eight times with one re-send in the middle; a silent receiver means the mouse is
// asleep or off.
// Sources:
//   - github.com/z750sasr/mchose-a5-pro-max-web-driver (MIT), docs/PROTOCOL.md: VID 2023, PIDs
//     F019 / F013 / F015, usage page FFFF, 64-byte feature reports, route / length / page / command
//     layout, A1 reply marker echoing page and command, page 00 carries the battery.
//   - github.com/HeyOkay/HaloBattery (MIT), providers/mchose.py: battery command 0x83 on page 0,
//     charging in reply byte 6 and level in byte 7 (after the A1 marker), the 34 ms / 8-read loop;
//     confirmed on a real A5 Pro Max on the F013 receiver (HaloBattery issue #149).
//   - github.com/Klegus/mchose-macos (MIT), README + mchose_config.py: the A5 Pro / A5 Pro Max id
//     table (F013, F015, F017, F018, F019, all VID 2023), interface 2 / usage page FFFF, 65-byte
//     feature reports with report id 0, 0xA1 = OK; reverse-engineered from MCHOSE's DriverCore.exe
//     and tested on an A5 Pro with the 4K receiver.
//   - github.com/OpenMouse-Project/mouse-protocol, docs/mchose-a5-gen1.md (documentation only):
//     the vendor's model table pairs F013 / F015 receivers with the F019 wired mouse.
// Not tested here on real hardware. F017 and F018 come from the Klegus table only.
// VID 2023 is not used by any other driver (Glorious' 0x2023 is a product id under VID 258A).
#include "drivers.h"

#include <algorithm>
#include <set>

namespace {

const USHORT kVid = 0x2023;
const BYTE kReportId = 0x00;
const BYTE kRouteMouse = 0x02;
const BYTE kPage = 0x00;
const BYTE kCmdBattery = 0x83;
const BYTE kOk = 0xA1;

struct Model {
    USHORT pid;
    const wchar_t* name;
    bool cable;
};

const Model kModels[] = {
    { 0xF013, L"MCHOSE A5 Pro / Pro Max", false },
    { 0xF015, L"MCHOSE A5 Pro / Pro Max", false },
    { 0xF017, L"MCHOSE A5 Pro", true },
    { 0xF018, L"MCHOSE A5 Pro Max", false },
    { 0xF019, L"MCHOSE A5 Pro Max", true },
};

std::vector<BYTE> Request(size_t len) {
    std::vector<BYTE> req(len, 0);
    req[0] = kReportId;
    req[3] = kRouteMouse;
    req[4] = 2;  // payload length, as in the reference
    req[5] = kPage;
    req[6] = kCmdBattery;
    return req;
}

// r[0] is the report id on Windows; the A1 marker follows. Returns true for a battery reply.
bool Parse(const std::vector<BYTE>& r, int& level, bool& charging) {
    if (r.size() < 9) return false;
    if (r[0] != kOk && r[1] != kOk) return false;
    size_t at = r[1] == kOk ? 1 : 0;
    if (r[at + 4] != kPage || r[at + 5] != kCmdBattery) return false;
    BYTE lv = r[at + 7];
    if (lv == 0xFF) return false;
    level = lv > 100 ? 100 : lv;  // the reference clamps to 100
    charging = r[at + 6] != 0;
    return true;
}

// 1 = battery read, 0 = collection works but the mouse did not answer, -1 = could not talk to it.
int Query(const HidColl& c, int& level, bool& charging) {
    HidDev d;
    if (!d.OpenFeatureOnly(c)) return -1;
    std::vector<BYTE> req = Request(c.featLen());
    if (!d.SetFeature(req)) return -1;
    Sleep(34);
    for (int attempt = 0; attempt < 8; attempt++) {
        if (attempt == 3) {
            if (!d.SetFeature(req)) return -1;
            Sleep(34);
        }
        std::vector<BYTE> r;
        if (d.GetFeature(kReportId, r) && Parse(r, level, charging)) return 1;
        Sleep(30);
    }
    return 0;
}

}  // namespace

void PollMchoseA5(PollContext& ctx) {
    static std::set<USHORT> answered;  // a silent receiver is only shown as asleep once its mouse has answered
    for (const Model& m : kModels) {
        if (ctx.IsClaimed(kVid, m.pid)) continue;
        auto colls = ctx.Find(kVid, m.pid, [](const HidColl& c) { return c.page() == 0xFFFF && c.featLen() >= 65; });
        // interface 2 first: that is where the references talk to it
        std::stable_sort(colls.begin(), colls.end(), [](const HidColl* a, const HidColl* b) { return (a->iface == 2) > (b->iface == 2); });
        int state = -1, level = 0;
        bool charging = false;
        for (const HidColl* c : colls) {
            int st = Query(*c, level, charging);
            if (st > state) state = st;
            if (st == 1) break;
        }
        if (state < 0) continue;
        DeviceStatus s;
        s.key = L"mchose-a5:" + Hex4(m.pid);
        s.name = m.name;
        s.kind = DeviceKind::Mouse;
        s.via = m.cable ? L"USB cable" : L"2.4 GHz receiver";
        if (state == 1) {
            answered.insert(m.pid);
            s.percent = level;
            s.charging = charging;
        } else {
            if (m.cable || !answered.count(m.pid)) continue;
            s.asleep = true;
        }
        ctx.Add(s, kVid, m.pid);
    }
}
