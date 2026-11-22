// Flydigi Vader 5 Pro (37D7:2401) and Apex 5 (37D7:2501), vendor collection FFA0:0001.
// Protocol reference: SDL/src/joystick/hidapi/SDL_hidapi_flydigi.c and
// https://github.com/HeyOkay/HaloBattery/issues/191 (Windows hardware traces).
// Apex 5 model ID 135 is not in SDL's table; it was seen on a real Apex 5 on the 2.4 GHz receiver.
// Only architecture/info queries are sent; no controller takeover or configuration writes.
#include "drivers.h"

namespace {
constexpr USHORT kVid = 0x37D7;

struct Model {
    USHORT pid;
    BYTE reportId;                      // output report ID for commands (0 = unnumbered)
    BYTE ids[6];                        // model IDs accepted from the info reply (data[5]); 0 = unused
    const wchar_t* name;
};

// Vader 5 Pro uses unnumbered output reports; Apex 5 uses report 03 (SDL FLYDIGI_V2_CMD_REPORT_ID).
const Model kModels[] = {
    { 0x2401, 0x00, { 130 }, L"Flydigi Vader 5 Pro" },
    { 0x2501, 0x03, { 128, 129, 133, 134, 135 }, L"Flydigi Apex 5" },
};

bool Reply(HidDev& d, BYTE command, std::vector<BYTE>& reply) {
    const ULONGLONG end = GetTickCount64() + 800;
    while (GetTickCount64() < end) {
        const int remaining = (int)(end - GetTickCount64());
        if (remaining <= 0 || d.Read(reply, remaining) < 0) return false;
        // HidDev strips a zero report ID. Some firmware retains a numbered input ID.
        if (reply.size() >= 4 && reply[0] == 0x04 && reply[1] == 0x5A && reply[2] == 0xA5)
            reply.erase(reply.begin());
        if (reply.size() >= 5 && reply[0] == 0x5A && reply[1] == 0xA5 && reply[2] == command)
            return true;
    }
    return false;
}

bool KnownId(const Model& m, BYTE id) {
    for (BYTE k : m.ids)
        if (k != 0 && k == id) return true;
    return false;
}

void PollModel(PollContext& ctx, const Model& m) {
    if (ctx.IsClaimed(kVid, m.pid)) return;
    for (const HidColl* c : ctx.Find(kVid, m.pid, [](const HidColl& c) {
             return c.page() == 0xFFA0 && c.usage() == 1 && c.inLen() >= 12 && c.outLen() >= 6;
         })) {
        HidDev d;
        if (!d.Open(*c)) continue;
        d.Flush();
        std::vector<BYTE> r;
        if (!d.Write({ m.reportId, 0x5A, 0xA5, 0x07, 0, 0 })) continue;
        bool modern = Reply(d, 0x07, r) && r[3] == 1 && r[4] == 0;
        if (!d.Write({ m.reportId, 0x5A, 0xA5, 0x01, 2, 0 }) || !Reply(d, 0x01, r)) continue;
        if (r.size() < 12 || !KnownId(m, r[5])) continue;  // confirmed model identity
        const int level = r[11] & 0x0F;
        const int state = r[11] >> 4;
        if (state > 2 || level > 5) continue;
        const int connection = r[6];
        if (modern ? (connection > 1) : (connection != 1 && connection != 2)) continue;
        DeviceStatus s;
        s.key = ctx.StableKey(L"flydigi:" + Hex4(m.pid));
        s.name = m.name;
        s.kind = DeviceKind::Gamepad;
        s.via = connection == (modern ? 1 : 2) ? L"2.4 GHz receiver" : L"USB cable";
        s.percent = state == 2 ? 100 : level * 20;
        s.charging = state == 1;
        ctx.Add(s, kVid, m.pid);
    }
}
}  // namespace

void PollFlydigi(PollContext& ctx) {
    for (const Model& m : kModels) PollModel(ctx, m);
}
