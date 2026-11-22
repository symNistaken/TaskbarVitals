// EWEADN (前行者) S7 / S9 series mice on EWEADN's own vendor id 372E.
//  * 372E:103D  "Gaming 2.4G" receiver shared by the S7 Max / Pro / Ultra and S9 Max / Pro / Ultra / Ultra+
//  * 372E:1027  EWEADN S9 Ultra on its USB cable
// Protocol from EWEADN's official web driver (new-hub.eweadn.cn): the device table in main.*.js lists
// both ids with solutionRovider "beiying" and hidReportId 3, and the "sdk-mouse-beiying" /
// "vendor-bytech" chunks hold the mouse SDK:
//  * Requests are 64-byte feature reports with report id 3 (usage page FF00, interface 2), read back
//    with GetFeature about 100 ms later (receiveDelay 100, up to 3 reads).
//    Request:  03 <sum> 50 <len> 00 <cmd> <sub> 00...   <sum> = (sum of the 63 bytes after the id) & FF
//    Reply:    03 50 <ack> <len> 00 <sub> <data...>     <ack> 0 = OK, data starts at byte 6
//  * 47 00 (checkWirelessOnline): data[0] = 1 when the mouse is linked to the receiver.
//  * 4F 81 (getBasics, a read command): data[0] bit 7 = charging, bits 0-6 = battery percent.
// Only these two read commands are sent; nothing is ever written to the mouse's settings.
#include "drivers.h"

namespace {
constexpr USHORT kVid = 0x372E;
constexpr USHORT kReceiver = 0x103D;
constexpr USHORT kS9UltraCable = 0x1027;
constexpr BYTE kReportId = 0x03;

bool Query(HidDev& d, BYTE cmd, BYTE sub, std::vector<BYTE>& data) {
    std::vector<BYTE> req(64, 0);
    req[0] = kReportId;
    req[2] = 0x50;  // CMD_TYPE
    req[5] = cmd;
    req[6] = sub;
    unsigned sum = 0;
    for (size_t i = 1; i < req.size(); ++i) sum += req[i];
    req[1] = static_cast<BYTE>(sum & 0xFF);
    if (!d.SetFeature(req)) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Sleep(100);
        std::vector<BYTE> reply;
        if (!d.GetFeature(kReportId, reply) || reply.size() < 8 || reply[0] != kReportId) continue;
        if (reply[2] != 0 || reply[4] != 0) continue;  // ack must be 0, single-packet reply (encap 0)
        size_t len = reply[3];
        if (len == 0 || 6 + len > reply.size()) continue;
        data.assign(reply.begin() + 6, reply.begin() + 6 + len);
        return true;
    }
    return false;
}
}  // namespace

void PollEweadn(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(kVid)) {
        if (pid != kReceiver && pid != kS9UltraCable) continue;  // other 372E products (AULA, keyboards) are not this protocol
        const bool cable = pid == kS9UltraCable;
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF00 && c.usage() == 1 && c.featLen() == 64;
             })) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            DeviceStatus s;
            s.key = L"eweadn:" + c->path;
            s.name = cable ? L"EWEADN S9 Ultra" : L"EWEADN wireless mouse";
            s.kind = DeviceKind::Mouse;
            s.via = cable ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 600;
            std::vector<BYTE> data;
            if (!cable) {
                if (!Query(d, 0x47, 0x00, data)) continue;
                if (data[0] != 1) {  // receiver present, mouse off or out of range
                    s.connected = false;
                    ctx.Add(s, vid, pid);
                    break;
                }
            }
            if (!Query(d, 0x4F, 0x81, data)) continue;
            const int level = data[0] & 0x7F;
            if (level > 100) continue;
            s.percent = level;
            s.charging = (data[0] & 0x80) != 0;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
