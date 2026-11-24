// Pulsar X2 V3 / X2 V3 eS and other Sonix-based Pulsar mice on vendor id 3710:
//   X2 V3:    USB cable 3402 ("X2 v3"), 1K dongle 5402 (confirmed by a user's --dump, issue #54)
//   X2 V3 eS: USB cable 3406, 8K dongle 5403
// Both dongles are shared with other Pulsar mice (Xlite V4, X3), so on a dongle the mouse gets a
// generic name. These do not speak the 17-byte report 08 protocol of the Kysona driver.
// Protocol from github.com/darthsoup/PulsarBattery (Sonix64Protocol.cs / Sonix64Backend.cs) and
// github.com/bluesceada/pulsar-mouse-linux (drivers/xlite_v4_wireless.py, verified on 5402 with an X3):
//   Feature report 0, 64-byte packet: [00][category][register][sub][00 ...][sum lo][sum hi],
//   sum = 16-bit sum of packet bytes 0..61 (little endian in bytes 62..63).
//   Battery read = 08 81 01. SET_FEATURE, then GET_FEATURE every ~50 ms until the reply echoes the
//   command: [status][08][81][01][..][..][percent] (packet byte 6, FF = no reading).
//   Status 01 = answered, 02 = still pending. 05 means "mouse not linked / asleep" in
//   pulsar-mouse-linux but "pending" in PulsarBattery, so we keep polling on 05 and only treat it as
//   "mouse off" if it is still the last answer when we give up.
//   The vendor collection (FFFF:0001) is on interfaces 2 and 3; interface 2 rejects SET_FEATURE, so
//   higher interfaces are tried first.
//   On the cable the mouse is charging; 0% from a dongle means the mouse is off or out of range.
//   On the 1K dongle 5402 two more reads come from PR #164 (Pulsar Fusion2 captures in pulsar-mouse-linux):
//   08 83 01 -> packet byte 6 = 1 while the mouse is on its charging cable, and 01 84 04 -> bytes 6..9 =
//   VID / PID (LE) of the paired mouse, used to name it (3710:3401 Xlite v4, 3710:3409 X3).
//   The 1K dongle has its own exchange (PollOneK below), checked on an Xlite v4, Windows 11:
//   - 05 is final there: the mouse is asleep and the request is never answered. It is shown as asleep
//     (last value dimmed) rather than as switched off.
//   - A request sent while another is pending on 02 is dropped, and resending them can leave every read
//     relayed to the mouse stuck on 02. So nothing is sent while the dongle shows 02, a pending request
//     is never re-sent, and one 1 s deadline covers all reads of a poll.
//   - The dongle is claimed whether or not it answers. Left unclaimed (the first poll after the mouse fell
//     asleep), kysona.cpp sent it its 17-byte report 08 probe, after which every read stayed on 02 until
//     the dongle was replugged and the mouse power-cycled.
// Feinmann FO1 8K (`3710:5404`, issue #3753) uses the same Sonix feature-report
// battery query `08 81 01` on interface 3 (FFFF:0001, 65/65/65 bytes).
// Captured and verified on real hardware in
// github.com/packerlschupfer/pulsar-mouse-linux (drivers/feinmann8k.py).
// The similarly named Feinmann F01 Noctua Edition uses a DIFFERENT Nordic protocol.
// Pulsar PRO Dongle (3710:5405) uses a B4 status request/reply on its 65-byte Generic Desktop
// collection. USBPcap captures provided in issue #911 show a zeroed B4 output report followed by a B4
// input report with 84 40 at bytes 4..5, byte 1 matching the web utility's 100% reading and byte 6
// changing from 0 to 1 on USB power: https://github.com/krisgfx9/Wireless-Device-Battery-Monitor/issues/911.
// Follow-up diagnostics also captured 85 40 on the dongle while charging toggled.
// The output report only requests status; it does not change device settings.
// The same B4 exchange is visible on the wired ZYWOO mouse (3710:7401); its B4 reply uses markers
// 84, 85, or 86 followed by 40.
#include "drivers.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <set>

namespace {

const USHORT kVid = 0x3710;

struct Product { USHORT pid; bool cable; const wchar_t* name; };
const Product kProducts[] = {
    { 0x3402, true, L"Pulsar X2 V3" },
    { 0x3406, true, L"Pulsar X2 V3 eS" },
    { 0x5402, false, nullptr },  // 1K dongle
    { 0x5403, false, nullptr },  // 8K dongle
    { 0x5404, false, L"Pulsar Feinmann FO1" },  // dedicated Feinmann 8K dongle (#3753)
    { 0x7401, true, nullptr },   // ZYWOO The Chosen X mouse over USB
    { 0x5405, false, nullptr },  // PRO Dongle
};

std::map<std::wstring, std::unique_ptr<HidDev>> g_b4Devices;
std::set<USHORT> g_knownB4Products;

bool DecodeB4Battery(const std::vector<BYTE>& r, int& pct, bool& charging, bool directMouse) {
    const bool markerValid = r.size() >= 7 &&
                             (r[4] == 0x84 || r[4] == 0x85 || (directMouse && r[4] == 0x86));
    if (!markerValid || r[0] != 0xB4 || r[1] > 100 || r[5] != 0x40 || r[6] > 1)
        return false;
    pct = r[1];
    charging = r[6] != 0;
    return true;
}

bool QueryB4Battery(HidDev& d, const HidColl& c, int& pct, bool& charging, bool directMouse) {
    if (!d.ok() && !d.Open(c)) return false;

    const ULONGLONG deadline = GetTickCount64() + 1000;
    std::vector<BYTE> request(65, 0);
    request[1] = 0xB4;
    std::vector<BYTE> r;
    int n = d.WriteAndRead(request, r, 1000);
    if (n < 0) {
        d.Close();
        return false;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (DecodeB4Battery(r, pct, charging, directMouse)) return true;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) break;
        const int timeoutMs = (int)std::min<ULONGLONG>(50, deadline - now);
        n = d.Read(r, timeoutMs);
        if (n < 0) {
            d.Close();
            return false;
        }
    }
    d.Close();
    return false;
}

enum class Reply { Battery, Offline, None };

// Reads the battery percent into `pct`. Offline = the dongle answered that the mouse is not linked.
// Sends one read (category / register / sub) and waits for its echo. On Reply::Battery, `w` holds the
// 64-byte reply packet (without the report id).
Reply Query(HidDev& d, BYTE cat, BYTE reg, BYTE sub, std::vector<BYTE>& w) {
    std::vector<BYTE> req(65, 0);  // [0] = report id 0, then the 64-byte packet
    BYTE* p = req.data() + 1;
    p[1] = cat;
    p[2] = reg;
    p[3] = sub;
    unsigned sum = 0;
    for (int i = 0; i < 62; i++) sum += p[i];
    p[62] = (BYTE)(sum & 0xFF);
    p[63] = (BYTE)(sum >> 8);
    bool sawOffline = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!d.SetFeature(req)) return Reply::None;
        for (int read = 0; read < 6; ++read) {
            Sleep(50);
            std::vector<BYTE> r;
            if (!d.GetFeature(0x00, r) || r.size() < 65) return Reply::None;
            if (r[2] != cat || r[3] != reg || r[4] != sub) continue;  // not our reply (yet)
            if (r[1] == 0x01) {
                w.assign(r.begin() + 1, r.end());
                return Reply::Battery;
            }
            sawOffline = r[1] == 0x05;  // 02 (or 05) = pending: keep polling
        }
    }
    return sawOffline ? Reply::Offline : Reply::None;
}

Reply ReadBattery(HidDev& d, int& pct) {
    std::vector<BYTE> w;
    Reply rep = Query(d, 0x08, 0x81, 0x01, w);
    if (rep != Reply::Battery) return rep;
    if (w[6] > 100) return Reply::None;  // FF = no reading
    pct = w[6];
    return Reply::Battery;
}

// Cabled PIDs the 1K dongle has been seen reporting for the mouse it carries (PR #164, pulsar-mouse-linux).
const wchar_t* PairedMouseName(USHORT vid, USHORT pid) {
    if (vid != kVid) return nullptr;
    switch (pid) {
        case 0x3401: return L"Pulsar Xlite v4";
        case 0x3409: return L"Pulsar X3";
        default: return nullptr;
    }
}

// --- 1K dongle 5402 ---

constexpr USHORT kOneKPid = 0x5402;
constexpr int kOneKDeadlineMs = 1000;
constexpr int kOneKPollMs = 10;
constexpr int kOneKStalePolls = 5;      // polls showing an older answer before the request counts as lost
constexpr int kOneKStaleResends = 2;    // resends allowed per request
constexpr int kOneKResendRoomMs = 350;  // no resend this close to the deadline: it would be left pending

enum class Link { Ok, NoLink, NoReply, IoError };

bool Echoes(const std::vector<BYTE>& r, const std::vector<BYTE>& req) {
    return r.size() >= 5 && req.size() >= 5 && r[2] == req[2] && r[3] == req[3] && r[4] == req[4];
}

class OneKDongle {
public:
    OneKDongle(HidDev& d, ULONGLONG deadline) : d_(d), deadline_(deadline) {}

    // On Link::Ok `w` holds the 64-byte reply packet (without the report id).
    Link Read(BYTE cat, BYTE reg, BYTE sub, std::vector<BYTE>& w) {
        std::vector<BYTE> req(65, 0);
        BYTE* p = req.data() + 1;
        p[1] = cat;
        p[2] = reg;
        p[3] = sub;
        unsigned sum = 0;
        for (int i = 0; i < 62; i++) sum += p[i];
        p[62] = (BYTE)(sum & 0xFF);
        p[63] = (BYTE)(sum >> 8);
        std::vector<BYTE> prev = prev_;
        prev_ = req;
        std::vector<BYTE> r;
        // The reply buffer is the dongle's: a request of an earlier poll (or of another program) may still
        // be pending there. Wait for it rather than send on top of it.
        if (!idle_) {
            for (;;) {
                if (!d_.GetFeature(0x00, r) || r.size() < 65) return Link::IoError;
                if (r[1] != 0x02) break;
                if (GetTickCount64() >= deadline_) return Link::NoReply;
                Sleep(kOneKPollMs);
            }
        }
        idle_ = false;
        if (!d_.SetFeature(req)) return Link::IoError;
        int sends = 1, stale = 0;
        bool shown = false;
        for (;;) {
            if (!d_.GetFeature(0x00, r) || r.size() < 65) return Link::IoError;
            if (Echoes(r, req)) {
                stale = 0;
                if (r[1] == 0x01) {
                    idle_ = true;
                    w.assign(r.begin() + 1, r.end());
                    return Link::Ok;
                }
                if (r[1] == 0x05) {
                    idle_ = true;
                    return Link::NoLink;
                }
                shown = true;  // pending: wait, never re-send
            } else if (r[1] == 0x01 && (shown || Echoes(r, prev))) {
                // The buffer went back to an older answer: the dongle lost the request.
                if (++stale >= kOneKStalePolls && sends <= kOneKStaleResends &&
                    GetTickCount64() + kOneKResendRoomMs < deadline_) {
                    if (!d_.SetFeature(req)) return Link::IoError;
                    sends++;
                    stale = 0;
                }
            } else {
                stale = 0;
            }
            if (GetTickCount64() >= deadline_) return Link::NoReply;
            Sleep(kOneKPollMs);
        }
    }

private:
    HidDev& d_;
    ULONGLONG deadline_;
    std::vector<BYTE> prev_;
    bool idle_ = false;  // the last read ended on a final status, so nothing of ours is pending
};

void PollOneK(PollContext& ctx) {
    bool present = false;
    for (const HidColl& c : ctx.colls) present |= c.vid == kVid && c.pid == kOneKPid;
    if (!present) return;
    ctx.Claim(kVid, kOneKPid);  // even when it does not answer: kysona.cpp's probe must never reach it
    for (const HidColl* c : ctx.Find(kVid, kOneKPid, [](const HidColl& c) {
             return c.iface == 3 && c.page() == 0xFFFF && c.usage() == 0x0001 && c.featLen() == 65;
         })) {
        HidDev d;
        if (!d.OpenFeatureOnly(*c)) continue;
        OneKDongle dongle(d, GetTickCount64() + kOneKDeadlineMs);
        std::vector<BYTE> w;
        Link bat = dongle.Read(0x08, 0x81, 0x01, w);
        if (bat == Link::IoError) continue;
        DeviceStatus s;
        s.key = L"pulsar:" + Hex4(kOneKPid);
        s.name = L"Pulsar wireless mouse";
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;
        if (bat == Link::Ok && w[6] >= 1 && w[6] <= 100) {
            s.percent = w[6];
            if (dongle.Read(0x08, 0x83, 0x01, w) == Link::Ok) s.charging = w[6] == 1;
        } else if (bat == Link::Ok) {
            s.connected = false;  // 0 or FF: no mouse paired or in range
        } else {
            s.asleep = true;  // 05 (asleep) or no answer in time: keep the last value
        }
        // Answered by the dongle itself, so it names the mouse while it sleeps too.
        if (bat != Link::NoReply && dongle.Read(0x01, 0x84, 0x04, w) == Link::Ok)
            if (const wchar_t* m = PairedMouseName((USHORT)(w[6] | (w[7] << 8)), (USHORT)(w[8] | (w[9] << 8))))
                s.name = m;
        ctx.Add(s, kVid, kOneKPid);
        break;
    }
}

}  // namespace

void PollPulsar(PollContext& ctx) {
    std::set<std::wstring> presentB4Devices;
    std::set<USHORT> enumeratedB4Products;
    for (const HidColl& c : ctx.colls)
        if (c.vid == kVid && (c.pid == 0x5405 || c.pid == 0x7401)) enumeratedB4Products.insert(c.pid);

    for (const Product& prod : kProducts) {
        if (ctx.IsClaimed(kVid, prod.pid)) continue;
        if (prod.pid == kOneKPid) {
            PollOneK(ctx);
            continue;
        }
        if (prod.pid == 0x5405 || prod.pid == 0x7401) {
            const bool directMouse = prod.pid == 0x7401;
            bool matchingCollection = false;
            bool reported = false;
            for (const HidColl* c : ctx.Find(kVid, prod.pid, [](const HidColl& c) {
                     return c.page() == 0x0001 && c.usage() == 0x0000 && c.inLen() == 65 && c.outLen() == 65;
                 })) {
                matchingCollection = true;
                presentB4Devices.insert(c->path);
                auto& slot = g_b4Devices[c->path];
                const bool fresh = !slot;
                if (fresh) {
                    slot = std::make_unique<HidDev>();
                    if (!slot->Open(*c)) {
                        g_b4Devices.erase(c->path);
                        continue;
                    }
                }
                int pct = -1;
                bool charging = false;
                if (!QueryB4Battery(*slot, *c, pct, charging, directMouse)) continue;
                g_knownB4Products.insert(prod.pid);
                DeviceStatus s;
                s.key = L"pulsar:" + Hex4(prod.pid);
                const std::wstring productName = directMouse ? HidProduct(*c) : L"";
                s.name = productName.empty() ? L"Pulsar wireless mouse" : productName;
                s.kind = DeviceKind::Mouse;
                s.via = directMouse ? L"USB cable" : L"2.4 GHz receiver";
                s.percent = pct;
                s.charging = charging;
                s.graceSec = 1800;
                ctx.Add(s, kVid, prod.pid);
                reported = true;
                break;
            }
            if (matchingCollection && !reported && !directMouse && g_knownB4Products.count(prod.pid)) {
                // The receiver is present but a previously detected wireless mouse did not answer.
                DeviceStatus s;
                s.key = L"pulsar:" + Hex4(prod.pid);
                s.name = L"Pulsar wireless mouse";
                s.kind = DeviceKind::Mouse;
                s.via = L"2.4 GHz receiver";
                s.asleep = true;
                s.graceSec = 1800;
                ctx.Add(s, kVid, prod.pid);
            }
            continue;
        }
        auto colls = ctx.Find(kVid, prod.pid, [&prod](const HidColl& c) {
            if (c.featLen() < 65) return false;
            if (prod.pid != 0x5404) return true;
            // The FO1 dongle exposes two FFFF collections; only interface 3 is the
            // documented 64-byte feature-report control channel.
            return c.iface == 3 && c.page() == 0xFFFF && c.usage() == 0x0001 &&
                   c.inLen() == 65 && c.outLen() == 65;
        });
        std::sort(colls.begin(), colls.end(), [](const HidColl* a, const HidColl* b) { return a->iface > b->iface; });
        for (const HidColl* c : colls) {
            HidDev d;
            if (!d.OpenFeatureOnly(*c)) continue;
            int pct = -1;
            Reply rep = ReadBattery(d, pct);
            if (rep == Reply::None) continue;
            DeviceStatus s;
            s.key = L"pulsar:" + Hex4(prod.pid);
            s.kind = DeviceKind::Mouse;
            if (prod.cable) {
                s.name = prod.name;
                s.via = L"USB cable";
                s.charging = true;
            } else {
                std::wstring n = HidProduct(*c);
                bool generic = n.empty() || n.find(L"Dongle") != std::wstring::npos ||
                               n.find(L"Receiver") != std::wstring::npos;
                // 5404 is specific to Feinmann FO1; 5402/5403 are shared receivers.
                s.name = prod.name ? prod.name : (generic ? L"Pulsar wireless mouse" : n);
                s.via = L"2.4 GHz receiver";
                s.connected = rep == Reply::Battery && pct > 0;
            }
            if (rep == Reply::Battery && (pct > 0 || prod.cable)) s.percent = pct;
            ctx.Add(s, kVid, prod.pid);
            break;
        }
    }
    for (auto it = g_b4Devices.begin(); it != g_b4Devices.end();)
        it = presentB4Devices.count(it->first) ? std::next(it) : g_b4Devices.erase(it);

    for (USHORT pid : g_knownB4Products) {
        if (enumeratedB4Products.count(pid)) continue;
        DeviceStatus s;
        s.key = L"pulsar:" + Hex4(pid);
        s.name = pid == 0x7401 ? L"Pulsar ZYWOO The Chosen X" : L"Pulsar wireless mouse";
        s.kind = DeviceKind::Mouse;
        s.via = pid == 0x7401 ? L"USB cable" : L"2.4 GHz receiver";
        s.connected = false;
        ctx.Add(s, kVid, pid);
    }
}
