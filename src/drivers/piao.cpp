// IPI PIAO / PIAO-2.4G wireless mouse (and its wired siblings) on vendor 372E.
//  * 372E:1014  PIAO-2.4G wireless receiver (product "IPI_PIAO", wireless)
//  * 372E:1056  IPI PIAO wired          (4117 is 0x1015, 4136 is 0x1028, 4182 is 0x1056)
// The protocol is the same feature-report family as EWEADN's "beiying" SDK that eweadn.cpp
// implements, confirmed both by capture on real hardware and by IPI's official web driver at
// https://shan.ipigame.cn/devices (app-Cn5AWPhI.js device table):
//   product_name "IPI_PIAO", vendor_id 14126 (0x372E), product_id 4116 (0x1014),
//   report_id_tx 3, send_payload_length 63, type_protocol "ms_pix_v1", type_channel
//   "feature_report", type_connection "wireless".
// Requests are 64-byte feature reports, report id 3, usage page FF00 / usage 1:
//   request  [03][sum][50][00][00][cmd][sub][00...]   sum = (sum of bytes 1..63) & 0xFF
//   reply    [03][50][status][len][00][cmd_echo][sub_echo][data...]
//             status 0 = OK, 2 = mouse asleep/link idle. Unlike EWEADN (where byte 5 is
//             the sub), PIAO echoes the command at byte 5 and the sub at byte 6, so the
//             battery value starts at byte 7.
//  * 47 00 (checkWirelessOnline): data[0] != 0 means the receiver link is alive.
//  * 4F 81 (getBasics): data[0] bit 7 = charging, bits 0-6 = battery percent. Confirmed
//             after the vendor's 2026-11 firmware update: raw 0xE4 (228) is shown by the
//             official driver as "100% + charging", i.e. 0x80 flag + 100 percent. Older
//             firmware showed the raw 228 directly (a driver display bug); the wire format
//             was always the same. While the mouse sleeps this answers status=2 with no data.
// Only these read commands are sent; nothing is written to the mouse configuration.
#include "drivers.h"

namespace {
constexpr USHORT kVid = 0x372E;
constexpr BYTE kReportId = 0x03;
constexpr int kLen = 64;

// true when pid is one of the PIAO ids; wireless flag set for the 1014 receiver.
bool IsPiao(USHORT pid, bool& wireless) {
    switch (pid) {
    case 0x1014: wireless = true;  return true;   // PIAO-2.4G receiver
    case 0x1015: wireless = false; return true;   // wired
    case 0x1028: wireless = false; return true;   // wired
    case 0x1056: wireless = false; return true;   // wired
    default: return false;
    }
}

bool Query(HidDev& d, BYTE cmd, BYTE sub, std::vector<BYTE>& data) {
    std::vector<BYTE> req(kLen, 0);
    req[0] = kReportId;
    req[2] = 0x50;  // CMD_TYPE
    req[5] = cmd;
    req[6] = sub;
    unsigned sum = 0;
    for (size_t i = 1; i < req.size(); ++i) sum += req[i];
    req[1] = static_cast<BYTE>(sum & 0xFF);
    if (!d.SetFeature(req)) return false;
    // PIAO's radio wakes on any feature query; a sleeping mouse needs a few frames before
    // it answers getBasics. Retry a bit longer and poke the link command in between.
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (attempt == 3 && cmd == 0x4F) {
            std::vector<BYTE> wake(64, 0);
            wake[0] = kReportId; wake[2] = 0x50; wake[5] = 0x47; wake[6] = 0x00;
            unsigned s2 = 0; for (size_t i = 1; i < wake.size(); ++i) s2 += wake[i];
            wake[1] = static_cast<BYTE>(s2 & 0xFF);
            (void)d.SetFeature(wake);  // poke the link to wake the mouse
        }
        Sleep(120);
        std::vector<BYTE> reply;
        if (!d.GetFeature(kReportId, reply) || reply.size() < 8) continue;
        if (reply[0] != kReportId) continue;
        if (reply[2] != 0 || reply[4] != 0) continue;
        // PIAO uses two reply layouts, both seen on real hardware / in IPI's web driver:
        //   link/status frame (47 00): [03][50][00][len][00][cmd][sub][data...]
        //   value frame   (4F 81):     [03][50][00][len][00][sub_lo][data...]
        // EWEADN echoes the full sub at byte 5. Accept whichever lines up.
        size_t offset = 0;
        // The 47 00 link frame echoes only the command at byte 5 (byte 6 is a counter),
        // so accept byte 5 == cmd on its own; data follows at byte 7.
        if (reply[5] == cmd) offset = 7;
        else if (reply[5] == sub || reply[5] == static_cast<BYTE>(sub & 0x0F) ||
                 reply[5] == static_cast<BYTE>(sub & 0x7F)) offset = 6; // sub echoed (possibly low byte)
        if (!offset) continue;
        size_t len = reply[3];
        if (len == 0 || offset + len > reply.size()) continue;
        data.assign(reply.begin() + offset, reply.begin() + offset + len);
        return true;
    }
    return false;
}
}  // namespace

void PollPiao(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(kVid)) {
        bool wireless = false;
        if (!IsPiao(pid, wireless)) continue;  // other 372E products (EWEADN, AULA) have their own drivers
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF00 && c.usage() == 1 && c.featLen() == kLen;
             })) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            DeviceStatus s;
            s.key = L"piao:" + Hex4(vid) + L":" + Hex4(pid);
            s.name = L"IPI PIAO wireless mouse";
            s.kind = DeviceKind::Mouse;
            s.via = wireless ? L"2.4 GHz receiver" : L"USB cable";
            s.graceSec = 600;
            std::vector<BYTE> data;
            if (wireless) {
                if (!Query(d, 0x47, 0x00, data)) continue;
                if (data.empty() || data[0] == 0) {  // mouse off or asleep
                    s.connected = false;
                    ctx.Add(s, vid, pid);
                    break;
                }
            }
            if (!Query(d, 0x4F, 0x81, data) || data.empty()) {
                // Receiver is linked but the mouse is asleep (getBasics answers status=2).
                // Keep the device visible as asleep so it is recognised and its last level can be retained.
                s.percent = -1;
                s.asleep = true;
                ctx.Add(s, vid, pid);
                break;
            }
            // Vendor firmware update confirmed the decode: bit 7 = charging, bits 0-6 =
            // percent. Raw 0xE4 => 100% + charging (official driver shows exactly that).
            const int level = data[0] & 0x7F;
            if (level > 100) continue;
            s.percent = level;
            s.charging = (data[0] & 0x80) != 0;
            s.full = s.charging && level >= 100;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
