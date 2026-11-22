// AULA X9 Ultra, verified on receiver 320F:22A0, FF1C:0092, 64-byte reports.
// This receiver ID is shared with V9 mice, so do not infer the model from the ID.
// Protocol reference: https://github.com/HeyOkay/HaloBattery/issues/325 and
// https://github.com/ahmedkhursheed23/HaloBattery/blob/main/providers/lofree.py
// Independently verified: START 01, battery 1A, END 02, report ID 04.
// Battery reply byte 8 = percent, byte 9 = 0 battery / 1 charging / 2 full.
// The receiver caches the level. A pushed AA link-down frame marks it asleep;
// lack of a heartbeat is NOT proof of disconnection. Zero is an uninitialised cache.
// Only read-state transactions; no settings, pairing or firmware commands.
#include "drivers.h"
#include "aula_mouse_protocol.h"

namespace {
constexpr USHORT kVid = 0x320F, kPid = 0x22A0;

bool WaitReply(HidDev& d, BYTE cmd, ULONGLONG end, std::vector<BYTE>& reply, bool& linkDown) {
    while (GetTickCount64() < end) {
        const LONGLONG left = (LONGLONG)end - (LONGLONG)GetTickCount64();
        if (left <= 0) break;
        std::vector<BYTE> r;
        if (d.Read(r, left > 100 ? 100 : (int)left) < 0) continue;
        if (AulaMouseProtocol::IsHeartbeat(r)) {
            linkDown = r[8] == 0;
            continue;
        }
        if (AulaMouseProtocol::IsReply(r, cmd) && (cmd != 0x1A || r.size() >= 10)) {
            reply = std::move(r);
            return true;
        }
    }
    return false;
}

bool ReadBattery(const HidColl& c, int& percent, bool& charging, bool& linkDown) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    // Entire transaction, including cleanup, is bounded to one second.
    const ULONGLONG end = GetTickCount64() + 1000;
    std::vector<BYTE> reply;
    if (!d.Write(AulaMouseProtocol::Request(1), 150)) return false;
    bool started = WaitReply(d, 1, end - 550, reply, linkDown);
    bool valid = false;
    if (started && d.Write(AulaMouseProtocol::Request(0x1A), 150) &&
        WaitReply(d, 0x1A, end - 200, reply, linkDown)) {
        valid = AulaMouseProtocol::ParseBattery(reply, percent, charging);
    }
    // End even after a missing acknowledgement, to leave no transaction open.
    if (d.Write(AulaMouseProtocol::Request(2), 100))
        WaitReply(d, 2, end, reply, linkDown);
    return valid;
}
}  // namespace

void PollAulaMouse(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& c) {
             return c.iface == 1 && c.page() == 0xFF1C && c.usage() == 0x0092 &&
                    c.inLen() == 64 && c.outLen() == 64;
         })) {
        if (HidProduct(*c) != L"AULA-8K Dongle") continue;
        DeviceStatus s;
        s.key = L"aula_mouse:320F:22A0";
        s.name = L"AULA wireless mouse (X9 / V9)";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;
        bool linkDown = false;
        int percent = -1;
        bool charging = false;
        if (ReadBattery(*c, percent, charging, linkDown) && !linkDown) {
            s.percent = percent;
            s.charging = charging;
        } else {
            s.asleep = true;
        }
        ctx.Add(s, kVid, kPid);
        return;
    }
}
