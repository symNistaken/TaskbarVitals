// EWEADN (前行者) S7 / S9 series mice on EWEADN's own vendor id 372E.
//  * 372E:1026  "Gaming 2.4G" receiver shared by the S9 Pro / Ultra / Max / Ultra+
//  * 372E:1033  the same S9 receiver (S9 Pro / Ultra)
//  * 372E:103D  the same receiver (S9 Pro / Ultra / Max / Ultra+; also used by S7 Max / Pro / Ultra)
//  * 372E:1055  the same S9 receiver (S9 Pro / Ultra / Max)
//  * 372E:1029 / 1027 / 105E / 108D  S9 Pro / Ultra / Max / Ultra+ on their USB cables
// The receiver ids and cable ids come from the device table in EWEADN's config.xml; all S9 models
// use the same BeiYing protocol. Hardware-tested on an S9 Pro 8K receiver (372E:1026, firmware v1052):
// 4F 81 returned 5D (93%).
// Protocol from EWEADN's official web driver (new-hub.eweadn.cn): the device table in main.*.js lists
// these ids with solutionRovider "beiying" and hidReportId 3, and the "sdk-mouse-beiying" /
// "vendor-bytech" chunks hold the mouse SDK:
//  * Requests are 64-byte feature reports with report id 3 (usage page FF00, interface 2), read back
//    with GetFeature about 100 ms later (receiveDelay 100, up to 3 reads).
//    Request:  03 <sum> 50 <len> 00 <cmd> <sub> 00...   <sum> = (sum of the 63 bytes after the id) & FF
//    Reply:    03 50 <ack> <len> 00 <sub> <data...>     <ack> 0 = OK, data starts at byte 6
//  * 47 00 (checkWirelessOnline): data[0] = 1 when the mouse is linked to the receiver.
//  * 4F 81 (getBasics, a read command): data[0] bit 7 = charging, bits 0-6 = battery percent.
// Royal Kludge M3 on the same vendor id speaks the same BeiYing protocol:
//  * 372E:1019  RK M3 on its 2.4 GHz dongle (hardware tested), 372E:102A  RK M3 on its USB cable (untested)
// From RK's web driver (drive.rkgaming.com, assets/index-*.js): both ids are "RK-M3" with protocolType BeiYing,
// and getBattery() sends 4F 81 with sequence number 2 in byte 4 (EWEADN sends 0), waits 200 ms, then
// accepts the reply only when byte 5 is 1. RK's getOnline() is empty, so no link check is sent.
// The reply echoes the sequence number in byte 4.
// Only these two read commands are sent; nothing is ever written to the mouse's settings.
#include "drivers.h"

namespace {
constexpr USHORT kVid = 0x372E;
constexpr BYTE kReportId = 0x03;

struct Model {
    USHORT pid;
    const wchar_t* name;
    const wchar_t* via;
    bool linkCheck;  // ask 47 00 first (receivers that report whether the mouse is linked)
    bool rk;         // RK's variant: sequence number 2, reply byte 5 must be 1, "rk:" key
};
constexpr Model kModels[] = {
    { 0x1026, L"EWEADN wireless mouse", L"2.4 GHz receiver", true, false },
    { 0x1033, L"EWEADN wireless mouse", L"2.4 GHz receiver", true, false },
    { 0x103D, L"EWEADN wireless mouse", L"2.4 GHz receiver", true, false },
    { 0x1055, L"EWEADN wireless mouse", L"2.4 GHz receiver", true, false },
    { 0x1027, L"EWEADN S9 Ultra", L"USB cable", false, false },
    { 0x1029, L"EWEADN S9 Pro", L"USB cable", false, false },
    { 0x105E, L"EWEADN S9 Max", L"USB cable", false, false },
    { 0x108D, L"EWEADN S9 Ultra+", L"USB cable", false, false },
    { 0x1019, L"RK M3", L"2.4 GHz receiver", false, true },
    { 0x102A, L"RK M3", L"USB cable", false, true },
};

const Model* FindModel(USHORT pid) {
    for (const Model& m : kModels)
        if (m.pid == pid) return &m;
    return nullptr;  // other 372E products (AULA, VGN, keyboards) are not this protocol
}

bool Query(HidDev& d, const Model& m, BYTE cmd, BYTE sub, std::vector<BYTE>& data) {
    const BYTE sn = m.rk ? 0x02 : 0x00;
    std::vector<BYTE> req(64, 0);
    req[0] = kReportId;
    req[2] = 0x50;  // CMD_TYPE
    req[4] = sn;
    req[5] = cmd;
    req[6] = sub;
    unsigned sum = 0;
    for (size_t i = 1; i < req.size(); ++i) sum += req[i];
    req[1] = static_cast<BYTE>(sum & 0xFF);
    if (!d.SetFeature(req)) return false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Sleep(m.rk && attempt == 0 ? 200 : 100);  // RK waits 200 ms; an earlier read can return the previous reply
        std::vector<BYTE> reply;
        if (!d.GetFeature(kReportId, reply) || reply.size() < 8 || reply[0] != kReportId) continue;
        if (reply[2] != 0 || reply[4] != sn) continue;  // ack must be 0, reply to this request
        if (m.rk && reply[5] != 1) continue;            // RK: the mouse did not answer
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
        const Model* m = FindModel(pid);
        if (!m) continue;
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF00 && c.usage() == 1 && c.featLen() == 64;
             })) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            DeviceStatus s;
            s.key = m->rk ? ctx.StableKey(L"rk:" + Hex4(pid)) : L"eweadn:" + c->path;  // EWEADN keeps its saved keys
            s.name = m->name;
            s.kind = DeviceKind::Mouse;
            s.via = m->via;
            s.graceSec = 600;
            std::vector<BYTE> data;
            if (m->linkCheck) {
                if (!Query(d, *m, 0x47, 0x00, data)) continue;
                if (data[0] != 1) {  // receiver present, mouse off or out of range
                    s.connected = false;
                    ctx.Add(s, vid, pid);
                    break;
                }
            }
            if (!Query(d, *m, 0x4F, 0x81, data)) continue;
            const int level = data[0] & 0x7F;
            if (level > 100) continue;
            s.percent = level;
            s.charging = (data[0] & 0x80) != 0;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
