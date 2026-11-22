// RAWM mice (Leviathan V4, ES21 / ER21 / SA-* / G11 families) and the SADES Dagger on their
// 2.4 GHz receivers or USB cable (Nordic VID 1915). Protocol notes are in rawm_protocol.h; the
// product list is RAWM HUB's device table (www.miracletek.net/game/device.php, used by the web app).
// Tested: Leviathan V4 (234A) on the RAWM HS Receiver (1915:2346).
// Only the requests the RAWM HUB app sends on connect are used: the device-info query and pings.
// Not covered: Leviathan V4 GT / NYX60 (VID 373E, a different protocol) and the HS keyboards.
#include "drivers.h"
#include "rawm_protocol.h"

#include <algorithm>
#include <ctime>

namespace {

constexpr USHORT kVid = 0x1915;

bool IsReceiver(USHORT pid) {
    switch (pid) {
    case 0x232A: case 0x232B: case 0x2335: case 0x2336: case 0x233B: case 0x233C:
    case 0x2346: case 0x2347: case 0x234B:
        return true;
    }
    return false;
}

bool IsMouse(USHORT pid) {
    switch (pid) {
    case 0x2328: case 0x2329: case 0x232C: case 0x232D: case 0x232E: case 0x232F: case 0x2330:
    case 0x2331: case 0x2332: case 0x2333: case 0x2334: case 0x2337: case 0x2338: case 0x2339:
    case 0x233A: case 0x233E: case 0x233F: case 0x2340: case 0x2341: case 0x2343: case 0x2344:
    case 0x2348: case 0x2349: case 0x234A: case 0x2352:
        return true;
    }
    return false;
}

bool Send(HidDev& d, const std::vector<BYTE>& event, int channel = Rawm::kNoChannel) {
    return d.Write(Rawm::Packet(event, channel), 200);
}

// Reads every report that arrives within `ms`, never past `deadline`.
void Pump(HidDev& d, Rawm::Link& link, int ms, ULONGLONG deadline) {
    ULONGLONG end = std::min(GetTickCount64() + ms, deadline);
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return;
        std::vector<BYTE> r;
        if (d.Read(r, left) < 0) return;
        link.Feed(r.data(), r.size());
    }
}

// Asks for the device info of `channel` (or of the USB device itself) and keeps pinging, since
// the device only returns data while the host sends. Returns once the info JSON is complete.
const Rawm::Status* Query(HidDev& d, Rawm::Link& link, int channel, ULONGLONG deadline) {
    Rawm::Stream& s = channel == Rawm::kNoChannel ? link.direct : link.channel0;
    if (!Send(d, Rawm::QueryEvent((std::uint64_t)time(nullptr)), channel)) return nullptr;
    while (GetTickCount64() < deadline) {
        if (!Send(d, Rawm::PingEvent(), channel)) return nullptr;
        Pump(d, link, 80, deadline);
        if (s.status().haveInfo) return &s.status();
    }
    return nullptr;
}

}  // namespace

void PollRawm(PollContext& ctx) {
    for (auto [vid, pid] : ctx.Products(kVid)) {
        bool receiver = IsReceiver(pid);
        if (!receiver && !IsMouse(pid)) continue;
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF00 && c.usage() == 0x0001 && c.inLen() == 65 && c.outLen() == 65;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            d.Flush();
            // Receiver hello (~5 ms), then the mouse's ~550-byte info over ESB (~200 ms on the
            // Leviathan V4). One absolute deadline covers both steps; a sleeping mouse uses all of it.
            const ULONGLONG deadline = GetTickCount64() + 1200;
            Rawm::Link link;
            const Rawm::Status* info = Query(d, link, Rawm::kNoChannel, deadline);
            if (!info) continue;
            if (receiver) {
                // The receiver must answer first; the mouse behind it is then ESB channel 0.
                info = Query(d, link, 0, deadline);
                if (!info) {
                    ctx.Claim(vid, pid);  // receiver found, mouse off or out of range
                    break;
                }
            }
            if (info->percent < 0) continue;

            long mousePid = 0;
            Rawm::JsonInt(info->json, "pi", mousePid);
            std::string dn = Rawm::JsonString(info->json, "dn");
            std::wstring name(dn.begin(), dn.end());
            if (name.empty()) name = L"wireless mouse";
            if (name.find(L"RAWM") == std::wstring::npos && name.find(L"SADES") == std::wstring::npos)
                name = L"RAWM " + name;

            DeviceStatus s;
            s.key = L"rawm:" + Hex4((USHORT)(mousePid > 0 && mousePid <= 0xFFFF ? mousePid : pid));
            s.name = name;
            s.kind = DeviceKind::Mouse;
            s.via = receiver ? L"2.4 GHz receiver" : L"USB cable";
            s.percent = info->percent;
            s.charging = info->charging;
            // A sleeping mouse stops answering through the receiver (seen on the Leviathan V4); the UI
            // keeps the last reading, dimmed, meanwhile.
            s.graceSec = 1800;

            // A charging mouse can be reachable through both its cable and the receiver: keep one
            // entry, preferring the cable's charging state.
            auto existing = std::find_if(ctx.out.begin(), ctx.out.end(), [&](const DeviceStatus& o) { return o.key == s.key; });
            if (existing == ctx.out.end()) ctx.Add(s, vid, pid);
            else {
                if (!receiver) *existing = s;
                ctx.Claim(vid, pid);
            }
            break;
        }
    }
}
