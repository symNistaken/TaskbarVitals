// SONiX keyboards on their 2.4 GHz dongle: AJAZZ AK820 MAX ("AJAZZ AK820 MAX 2.4G", 05AC:024F, tested),
// AULA F108 Pro (same 05AC:024F dongle), AJAZZ AK980 V2 (0C45:FEFE) and AJAZZ AK820 Pro (0C45:FDFD,
// some units ship a 0C45:FEFE "2.4G Dongle"). The 05AC dongle reuses Apple's vendor id, so the dongles
// are also matched by their vendor collection: usage page FF60 / usage 61, 33-byte input
// and output reports (report id 0 + 32 bytes). These dongles require interface 3;
// EPOMAKER P75 (342D:E479, request #2525, hardware untested) uses interface 1;
// RSQ_20055_2.4G (342D:E411, issue #3808, untested) reports interface 1, FF60:0061, 33/33;
// Galaxy100 (342D:E489, request #3802, hardware untested) reports interface 2.
//   request [20 01 00 ... sum]          sum = byte sum of the 31 bytes before it
//   reply   [20 01 00 level ...]        level in percent
// A reply is recognised by its first three bytes (the echoed command), as the AK820 Pro tools do; its
// checksum is not checked. The keyboard only reports its level while it is switched to 2.4G mode.
// The protocol is read-only.
// MCHOSE K87 receivers on the same 05AC:024F id expose a different layout (the BY collection on
// interface 1, no FF60 on interface 3); aula.cpp runs first and reads those.
// From github.com/donglover69/sonix-keyboard-studio (README, "2.4G dongles"); the AK820 Pro receiver
// 0C45:FDFD and the echo match from github.com/deddroid/ak820-rs (src/device.rs, src/protocol.rs) and
// github.com/usharik/ak820-studio (Sources/AK820Kit/Keyboard.swift).
// Lofree Flow2 84 (OE927) on its 388D:0002 receiver (PR #4162): only with the experimental custom
// firmware LF2-v1 from github.com/softerist/lofree; stock firmware has no battery reading. LF2-v1
// answers the same 20 01 request with a full 32-byte reply:
//   [20 01 00 level status 'L' 'F' '2' 01 ... sum]  status 1 = level valid (0% is a real reading),
//   status 0 with level FF = no reading yet; byte 31 = byte sum of bytes 0..30.
// The receiver is told apart from the cable (same USB id) by its extra interface-0 collection
// (usage page 1, usage 0, 64-byte reports, no feature report).
#include "drivers.h"

#include <map>

namespace {

// iface < 0: match the exact vendor collection on any interface.
struct Dongle { USHORT vid, pid; int iface; };
const Dongle kDongles[] = {
    { 0x05AC, 0x024F, 3 },
    { 0x0C45, 0xFEFE, 3 },
    { 0x0C45, 0xFDFD, 3 },
    { 0x342D, 0xE479, -1 },  // EPOMAKER P75 (hardware untested)
    { 0x342D, 0xE411, 1 },   // RSQ_20055_2.4G (issue #3808, hardware untested)
    { 0x342D, 0xE489, 2 },   // EPOMAKER Galaxy100 (issue #3802, hardware untested)
    { 0x342D, 0xE485, -1 },  // RK Royal Kludge R75 (PR #2593, hardware untested)
    { 0x342D, 0xE4BC, -1 },  // Redragon Arlokks K742B (PR #2594, hardware untested)
    { 0x342D, 0xE4C2, -1 },  // M1 V5 VIA (PR #2595, hardware untested)
    { 0x342D, 0xE407, -1 },  // Lucky65V2 (PR #2597, hardware untested)
    { 0x388D, 0x0003, -1 },  // Lofree Flow2 (PR #2598, hardware untested)
    { 0x0DB0, 0x0CA0, -1 },  // MSI FORGE TKL W FRIEREN EDITION (PR #2599, hardware untested)
    { 0x36B0, 0x30BD, -1 },  // Essential75 (PR #2607, hardware untested)
    { 0x342D, 0xE51A, -1 },  // NUT65 (PR #2608, hardware untested)
    { 0x342D, 0xE487, -1 },  // RK 2.4G Dongle (PR #2609, hardware untested)
    { 0x342D, 0xE537, -1 },  // WEIKAV MAX102 (PR #2610, hardware untested)
    { 0x19F5, 0x3245, -1 },  // NuPhy Air75V2 (PR #2613, hardware untested)
    { 0x362D, 0xD028, -1 },  // SKN Qinglong (PR #2614, hardware untested)
    { 0x362D, 0xD030, -1 },  // Lemokey Link (PR #2616, hardware untested)
    { 0x362D, 0xD101, -1 },  // Wireless Link (PR #2617, hardware untested)
    { 0x342D, 0xE49F, -1 },  // Spade65-QMK-2.4GHz (PR #2619, hardware untested)
    { 0x0DB0, 0x8A8F, -1 },  // MSI FORGE GK600 TKL WIRELESS (PR #4117, hardware untested)
    { 0x373B, 0x111F, -1 },  // ATK A87Pro (PR #4118, hardware untested)
    { 0x55D4, 0x0664, -1 },  // W-SOFLE keyboard (PR #4119, hardware untested)
    { 0xFFFE, 0x003D, -1 },  // CM81 Zeno (PR #4120, hardware untested)
    { 0xFFFE, 0x0047, -1 },  // Vortex NS75 (PR #4121, hardware untested)
    { 0x373B, 0x1053, -1 },  // MAD60 (PR #4122, hardware untested)
    { 0x6D66, 0x8888, -1 },  // Zoom 75 (PR #4123, hardware untested)
    { 0x373B, 0x1151, -1 },  // ATK A98 Pro (PR #4124, hardware untested)
    { 0x373B, 0x1193, -1 },  // REKT Hiro 60 (PR #4125, hardware untested)
    { 0x373B, 0x11FF, -1 },  // ATK YOGO 75 PRO (PR #4126, hardware untested)
    { 0x6D66, 0x8572, -1 },  // Meletrix Zoom64 (PR #4127, hardware untested)
    { 0x36B0, 0x3122, -1 },  // CIDOO QK104PRO (PR #4128, hardware untested)
    { 0x342D, 0xE4C5, -1 },  // M1 V5 VIA 2.4G KB (PR #4129, hardware untested)
    { 0x342D, 0xE471, -1 },  // EPOMAKER TIDE 75 (PR #4130, hardware untested)
    { 0x19F5, 0x32F5, -1 },  // NuPhy Halo75 V2 (PR #4131, hardware untested)
    { 0x36B0, 0x3035, -1 },  // CIDOO QK61 (PR #4132, hardware untested)
    { 0x36B0, 0x3005, -1 },  // Linky87 (PR #4133, hardware untested)
    { 0x342D, 0xEA80, -1 },  // IO Custom 68 2.4G (PR #4134, hardware untested)
    { 0x36B0, 0x2003, -1 },  // wireless 2.4G Dongle (PR #4135, hardware untested)
    { 0x362D, 0xD107, -1 },  // JZM5 (PR #4136, hardware untested)
};

const size_t kLen = 32;  // payload bytes after the report id; the last one is the checksum

BYTE Sum(const std::vector<BYTE>& b, size_t from, size_t n) {
    unsigned s = 0;
    for (size_t i = from; i < from + n; i++) s += b[i];
    return (BYTE)(s & 0xFF);
}

// Returns the level byte, or -1 when the dongle did not answer.
int QueryLevel(HidDev& d) {
    std::vector<BYTE> req(1 + kLen, 0);  // [0] = report id 0
    req[1] = 0x20;
    req[2] = 0x01;
    req[kLen] = Sum(req, 1, kLen - 1);
    d.Flush();
    if (!d.Write(req)) return -1;
    ULONGLONG end = GetTickCount64() + 500;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return -1;
        std::vector<BYTE> r;
        int n = d.Read(r, left);  // report id 0 is stripped: r[0] is the first payload byte
        if (n < 0) return -1;
        if (n < 4 || r[0] != 0x20 || r[1] != 0x01 || r[2] != 0x00) continue;
        return r[3];
    }
}

enum class Flow2 { Level, NoReading, Stock, Silent };

// One LF2-v1 query. A stock-firmware echo (no LF2 signature) ends it at once.
Flow2 QueryFlow2(HidDev& d, int& level) {
    std::vector<BYTE> req(1 + kLen, 0);
    req[1] = 0x20;
    req[2] = 0x01;
    req[kLen] = Sum(req, 1, kLen - 1);
    d.Flush();
    if (!d.Write(req)) return Flow2::Silent;
    // LF2-v1 waits for fresh radio telemetry, which can take longer than the usual 500 ms.
    const ULONGLONG end = GetTickCount64() + 2000;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return Flow2::Silent;
        std::vector<BYTE> r;
        int n = d.Read(r, left);
        if (n < 0) return Flow2::Silent;
        if (n < 4 || r[0] != 0x20 || r[1] != 0x01 || r[2] != 0x00) continue;
        if (n != (int)kLen || r[5] != 'L' || r[6] != 'F' || r[7] != '2' || r[8] != 1) return Flow2::Stock;
        if (r[31] != Sum(r, 0, 31)) continue;
        if (r[4] == 0 && r[3] == 0xFF) return Flow2::NoReading;
        if (r[4] != 1 || r[3] > 100) continue;
        level = r[3];
        return Flow2::Level;
    }
}

// Receivers that answered like stock firmware are not asked again until they are replugged.
std::map<std::wstring, bool> g_flow2Stock;

void PollFlow2Receiver(PollContext& ctx) {
    constexpr USHORT vid = 0x388D, pid = 0x0002;
    std::map<std::wstring, bool> seen;
    if (ctx.IsClaimed(vid, pid)) return;
    bool receiver = !ctx.Find(vid, pid, [](const HidColl& c) {
        return c.iface == 0 && c.page() == 0x0001 && c.usage() == 0 && c.inLen() == 64 && c.outLen() == 64 &&
               c.featLen() == 0;
    }).empty();
    if (!receiver) {
        g_flow2Stock.clear();
        return;
    }
    for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
             return c.iface == 2 && c.page() == 0xFF60 && c.usage() == 0x0061 && c.inLen() == kLen + 1 &&
                    c.outLen() == kLen + 1;
         })) {
        if (g_flow2Stock.count(c->path)) {
            seen[c->path] = true;
            continue;
        }
        HidDev d;
        if (!d.Open(*c)) continue;
        int level = -1;
        Flow2 got = QueryFlow2(d, level);
        if (got == Flow2::Silent) got = QueryFlow2(d, level);  // the first request after idle can be lost
        if (got == Flow2::Stock) {
            seen[c->path] = true;
            continue;
        }
        if (got == Flow2::Silent) continue;
        DeviceStatus s;
        s.key = L"sonix:388D:0002";
        s.name = L"Lofree Flow 2";
        s.kind = DeviceKind::Keyboard;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;  // keep the last reading while the keyboard sleeps
        if (got == Flow2::Level) s.percent = level;
        ctx.Add(s, vid, pid);
        break;
    }
    for (auto& [path, stock] : seen) g_flow2Stock[path] = stock;
}

std::wstring KeyboardName(const HidColl& c) {
    std::wstring name = HidProduct(c);
    const std::wstring suffix = L" 2.4G";
    if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        name.resize(name.size() - suffix.size());
    return name.empty() ? L"Wireless keyboard" : name;
}

}  // namespace

void PollSonix(PollContext& ctx) {
    for (const Dongle& m : kDongles) {
        if (ctx.IsClaimed(m.vid, m.pid)) continue;
        for (const HidColl* c : ctx.Find(m.vid, m.pid, [&m](const HidColl& c) {
                 return (m.iface < 0 || c.iface == m.iface) && c.page() == 0xFF60 && c.usage() == 0x0061 &&
                        c.outLen() == 1 + kLen && c.inLen() == 1 + kLen;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            // The first request can go unanswered while the 2.4 GHz link wakes up; retry once.
            int level = QueryLevel(d);
            if (level < 0) level = QueryLevel(d);
            if (level < 0) continue;
            DeviceStatus s;
            s.key = L"sonix:" + Hex4(m.vid) + L":" + Hex4(m.pid);
            s.name = KeyboardName(*c);
            s.kind = DeviceKind::Keyboard;
            s.via = L"2.4 GHz receiver";
            s.connected = level >= 1 && level <= 100;  // 0: keyboard off, out of range or in another mode
            if (s.connected) s.percent = level;
            s.graceSec = 1800;  // keep the last value if a sleeping keyboard stops answering
            ctx.Add(s, m.vid, m.pid);
            break;
        }
    }
    PollFlow2Receiver(ctx);
}
