// Logitech / ASTRO wireless headsets (A50 X, A50 Gen 5, A50 Gen 4, A50 Gen 3, A20 Gen 1/2).
//
// 1. ASTRO A50 X (046D:0B0B) and A50 Gen 5 (046D:0B1C):
//    Vendor HID collection on usage page FF32 / usage 0074 (interface 8), 64-byte reports, ID 02:
//      request: [02][0C][03][00][06][0C][00...]
//      reply:   [02][0C][len][00][06][handle][percent][...][charging]
//    (Protocol from Sapd/HeadsetControl logitech_astro_a50.hpp.)
//
// 2. ASTRO A50 Gen 4 ("Mimas"), A50 Gen 3 ("Europa") and A20 ("Aquarius"), VID 9886:
//    * Gen 4 base stations: 002C (PlayStation/PC), 002D / 002A (Xbox/PC)
//    * Gen 3 base stations: 0015, 0014, 0018, 001D, 0026
//    * A20 transmitters:    0037, 0038, 0039, 0043, 0044, 0045
//    Vendor HID collection on usage page FF32 or FF31, usage 0074 (interface 6), 64-byte reports,
//    report ID 02:
//      request: [02][cmd]
//      reply:   [02][status][len][payload...]   (status 02 = OK, 00 = accepted, 01 = error)
//    cmd 0x54 (headset status) -> payload[0]: bit 0 (0x01) = docked, bit 1 (0x02) = live 2.4 GHz link
//    cmd 0x7C (battery status) -> payload[0]: bit 7 (0x80) = charging, bits 0..6 = battery % (0..100)
//    (Protocol from tdryer/eh-fifty, nichtlegacy/AstroBar, Sapd/HeadsetControl logitech_astro_a50_gen4.hpp.)
//
//    A50 Gen 3 (9886:0015, base TXDP01) answers 0x54 but rejects 0x7C (status 01, "HID_E..." text).
//    Its battery is in the "current UI" read instead: request [02][72][01][01], reply data is four
//    [02][field][value][00] groups; the field-04 group (data[12..15]) holds battery % in bits 0..6 and
//    charging in bit 7. On Gen 4, 0x72 means something else, so it is only used after 0x7C fails.
//    (Protocol from BenStein1/OpenAstroCC docs/A50-PROTOCOL.md, tested on 9886:0015.)
//
//    Coexistence with Astro Command Center (Microsoft Store / UWP):
//    The UWP app opens FF32:0074 with FileAccessMode.ReadWrite, placing an exclusive kernel lock
//    (ShareMode = 0) on the collection while open. EnumHid() recovers the VID/PID from the device path
//    when CreateFileW returns ERROR_SHARING_VIOLATION, so PollAstro keeps the headset visible (with
//    its last cached reading) and resumes live polling as soon as Astro Command Center is closed.
#include "drivers.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace {

constexpr USHORT kVidAstro = 0x9886;
constexpr USHORT kVidLogitech = 0x046D;

struct LegacyModel { USHORT pid; const wchar_t* name; };
constexpr LegacyModel kLegacyModels[] = {
    // A50 Gen 4 (Mimas)
    { 0x002C, L"ASTRO A50 Gen 4" },
    { 0x002D, L"ASTRO A50 Gen 4" },
    { 0x002A, L"ASTRO A50 Gen 4" },
    // A50 Gen 3 (Europa)
    { 0x0015, L"ASTRO A50 Gen 3" },
    { 0x0014, L"ASTRO A50 Gen 3" },
    { 0x0018, L"ASTRO A50 Gen 3" },
    { 0x001D, L"ASTRO A50 Gen 3" },
    { 0x0026, L"ASTRO A50 Gen 3" },
    // A20 Wireless (Aquarius)
    { 0x0037, L"ASTRO A20 Wireless" },
    { 0x0038, L"ASTRO A20 Wireless" },
    { 0x0039, L"ASTRO A20 Wireless" },
    { 0x0043, L"ASTRO A20 Wireless" },
    { 0x0044, L"ASTRO A20 Wireless" },
    { 0x0045, L"ASTRO A20 Wireless" },
};

struct CachedState {
    int percent = -1;
    bool charging = false;
    bool docked = false;
};
std::map<USHORT, CachedState> g_cache;

// Gen 4 bases declare report ID 02, so the frame [02][cmd] is written as-is. Some Gen 3 base
// firmware (9886:0015, TXDP01) declares no report ID: Windows then rejects a buffer starting with
// 02, and the same frame has to be sent behind a 00 report-ID byte. Remember what works per PID.
enum class Framing { Unknown, ReportId02, NoReportId };
std::map<USHORT, Framing> g_framing;

enum class QResult { Ok, Error, NoReply, WriteFailed };

std::vector<BYTE> LegacyFrame(Framing f, BYTE cmd, const std::vector<BYTE>& payload = {}) {
    std::vector<BYTE> v;
    if (f == Framing::NoReportId) v.push_back(0x00);
    v.push_back(0x02);
    v.push_back(cmd);
    if (!payload.empty()) {
        v.push_back((BYTE)payload.size());
        v.insert(v.end(), payload.begin(), payload.end());
    }
    return v;
}

// reply: the whole frame starting at [02][status][len]...
QResult QueryOnce(HidDev& d, Framing f, BYTE cmd, const std::vector<BYTE>& payload, std::vector<BYTE>& reply) {
    d.Flush();
    if (!d.Write(LegacyFrame(f, cmd, payload))) return QResult::WriteFailed;
    ULONGLONG end = GetTickCount64() + 500;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 4) break;
        if (r[0] != 0x02) continue;
        if (r[1] == 0x01) return QResult::Error;  // understood, but refused (unknown command / not linked)
        if (r[1] == 0x02) {
            reply = r;
            return QResult::Ok;
        }
    }
    return QResult::NoReply;
}

QResult QueryLegacy(HidDev& d, USHORT pid, BYTE cmd, const std::vector<BYTE>& payload, std::vector<BYTE>& reply) {
    Framing known = g_framing.count(pid) ? g_framing[pid] : Framing::Unknown;
    Framing order[2] = { Framing::ReportId02, Framing::NoReportId };
    if (known == Framing::NoReportId) std::swap(order[0], order[1]);
    QResult last = QResult::NoReply;
    for (Framing f : order) {
        last = QueryOnce(d, f, cmd, payload, reply);
        if (last == QResult::Ok || last == QResult::Error) {
            g_framing[pid] = f;
            return last;
        }
        if (known != Framing::Unknown) break;  // framing already proven: a miss is a real miss
    }
    return last;
}

QResult QueryLegacy(HidDev& d, USHORT pid, BYTE cmd, BYTE& value) {
    std::vector<BYTE> r;
    QResult res = QueryLegacy(d, pid, cmd, {}, r);
    if (res == QResult::Ok) value = r[3];
    return res;
}

// Gen 3 battery from the 0x72 "current UI" read: data[12..15] = [02][04][battery][00].
bool QueryGen3Battery(HidDev& d, USHORT pid, BYTE& value) {
    std::vector<BYTE> r;
    if (QueryLegacy(d, pid, 0x72, { 0x01 }, r) != QResult::Ok) return false;
    if ((r[2] & 0x3F) < 16 || r.size() < 3 + 16) return false;
    const BYTE* data = r.data() + 3;
    if (data[12] != 0x02 || data[13] != 0x04 || (data[14] & 0x7F) > 100) return false;
    value = data[14];
    return true;
}

bool PollLegacyCollection(const HidColl& c, const wchar_t* name, DeviceStatus& s) {
    s.key = L"astro:" + Hex4(c.pid);
    s.name = name;
    s.kind = DeviceKind::Headset;
    s.via = L"USB base station";
    s.graceSec = 1800;

    HidDev d;
    if (d.Open(c)) {
        // 0x54 (dock / link status) is optional: some Gen 3 firmware may not answer it.
        BYTE st = 0;
        bool haveStatus = QueryLegacy(d, c.pid, 0x54, st) == QResult::Ok;
        bool docked = haveStatus && (st & 0x01) != 0;
        if (haveStatus) {
            s.connected = docked || (st & 0x02) != 0;
            if (!s.connected) return true;
        }

        BYTE bat = 0;
        QResult rb = QueryLegacy(d, c.pid, 0x7C, bat);
        if (rb == QResult::Error && haveStatus && QueryGen3Battery(d, c.pid, bat)) rb = QResult::Ok;  // A50 Gen 3
        if (rb == QResult::Ok && (bat & 0x7F) <= 100) {
            int pct = bat & 0x7F;
            s.connected = true;
            s.percent = pct;
            s.charging = (bat & 0x80) != 0 || (docked && pct < 100);
            if (docked) s.detail = L"docked";
            g_cache[c.pid] = { s.percent, s.charging, docked };
            return true;
        }
        if (rb == QResult::Error && !haveStatus) {  // base answered: headset off or out of range
            s.connected = false;
            return true;
        }
        return haveStatus;
    }

    // Exclusive lock held by Astro Command Center (UWP): keep the headset visible with cached reading.
    s.connected = true;
    auto it = g_cache.find(c.pid);
    if (it != g_cache.end() && it->second.percent >= 0) {
        s.percent = it->second.percent;
        s.charging = it->second.charging;
        s.detail = it->second.docked ? L"docked, Astro Command Center" : L"Astro Command Center";
    } else {
        s.detail = L"Astro Command Center";
    }
    return true;
}

bool IsLegacyVendorColl(const HidColl& c) {
    return (c.page() == 0xFF32 || c.page() == 0xFF31 || c.page() == 0xFF00) && c.inLen() >= 64 && c.outLen() >= 64;
}

}  // namespace

void PollAstro(PollContext& ctx) {
    // 1. ASTRO A50 X (046D:0B0B) and A50 Gen 5 (046D:0B1C)
    for (const auto& c : ctx.colls) {
        if (c.vid != kVidLogitech || (c.pid != 0x0B0B && c.pid != 0x0B1C) ||
            ctx.IsClaimed(c.vid, c.pid) || c.page() != 0xFF32 || c.usage() != 0x0074 ||
            c.inLen() != 64 || c.outLen() != 64) continue;
        HidDev d;
        if (!d.Open(c)) continue;
        d.Flush();
        // No payload: read battery only; never change audio or headset settings.
        std::vector<BYTE> request(64, 0);
        request[0] = 0x02;
        request[1] = 0x0C;
        request[2] = 3;
        request[4] = 0x06;
        request[5] = 0x0C;
        if (!d.Write(request)) continue;
        const auto deadline = GetTickCount64() + 1000;
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto now = GetTickCount64();
            if (now >= deadline) break;
            std::vector<BYTE> reply;
            int n = d.Read(reply, (int)(deadline - now));
            if (n < 0) break;
            // Ignore unsolicited volume/mix events and malformed or partial frames.
            if (n < 9 || reply.size() < 9 || reply[0] != 0x02 || reply[1] != 0x0C ||
                reply[2] < 6 || (size_t)reply[2] + 3 > reply.size() ||
                (int)reply[2] + 3 > n || reply[3] != 0 || reply[4] != 0x06 ||
                (reply[5] != 0 && reply[5] != 0x0C) || reply[6] > 100) continue;
            DeviceStatus s;
            s.key = ctx.StableKey(L"astro:" + Hex4(c.pid));
            s.name = c.pid == 0x0B0B ? L"ASTRO A50 X" : L"ASTRO A50 Gen 5";
            s.kind = DeviceKind::Headset;
            s.via = L"USB base station";
            s.percent = reply[6];
            s.charging = reply[8] != 0;
            ctx.Add(s, c.vid, c.pid);
            break;
        }
    }

    // 2. ASTRO A50 Gen 4 / Gen 3 and A20 Wireless (VID 9886)
    for (const LegacyModel& m : kLegacyModels) {
        if (ctx.IsClaimed(kVidAstro, m.pid)) continue;
        for (const HidColl* c : ctx.Find(kVidAstro, m.pid, [](const HidColl& c) {
                 bool validPage = IsLegacyVendorColl(c);
                 bool lockedVendorIf = c.page() == 0 && (c.iface == 6 || c.iface == -1);
                 return validPage || lockedVendorIf;
             })) {
            DeviceStatus s;
            if (PollLegacyCollection(*c, m.name, s)) {
                ctx.Add(s, kVidAstro, m.pid);
                break;
            }
        }
    }
}

// Raw 0x54 / 0x7C / 0x72 exchanges with every VID 9886 base station, in both framings, for `--dump`.
// Read-only requests only.
std::wstring DumpAstroFrames() {
    std::wstring out;
    for (const HidColl& c : EnumHid()) {
        if (c.vid != kVidAstro || !IsLegacyVendorColl(c)) continue;
        wchar_t head[80];
        swprintf(head, 80, L"\nAstro %04X:%04X (IF %d, %04X:%04X) raw frames:\n", c.vid, c.pid, c.iface, c.page(), c.usage());
        out += head;
        HidDev d;
        if (!d.Open(c)) {
            out += L"  cannot open (error " + std::to_wstring(GetLastError()) + L"), Astro Command Center running?\n";
            continue;
        }
        struct Probe { BYTE cmd; std::vector<BYTE> payload; };
        const Probe probes[] = { { 0x54, {} }, { 0x7C, {} }, { 0x72, { 0x01 } } };
        for (Framing f : { Framing::ReportId02, Framing::NoReportId }) {
            for (const Probe& p : probes) {
                wchar_t line[64];
                swprintf(line, 64, L"  %ls cmd %02X: ", f == Framing::ReportId02 ? L"[02 cmd]   " : L"[00 02 cmd]", p.cmd);
                out += line;
                d.Flush();
                if (!d.Write(LegacyFrame(f, p.cmd, p.payload))) {
                    out += L"write failed (error " + std::to_wstring(GetLastError()) + L")\n";
                    continue;
                }
                std::vector<BYTE> r;
                int frames = 0;
                ULONGLONG end = GetTickCount64() + 500;
                while (frames < 3 && (LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
                    if (d.Read(r, (int)std::max<LONGLONG>(1, (LONGLONG)end - (LONGLONG)GetTickCount64())) < 0) break;
                    if (frames++) out += L"                       ";
                    for (size_t i = 0; i < r.size() && i < 20; ++i) {
                        wchar_t hx[4];
                        swprintf(hx, 4, L" %02X", r[i]);
                        out += hx;
                    }
                    out += L"\n";
                }
                if (!frames) out += L"no reply\n";
            }
        }
    }
    return out;
}
