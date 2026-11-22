// MCHOSE "new" mice (VID 3837): the K7 V2 Ultra+ on its 2.4 GHz receiver (3837:1014) or on its USB
// cable (3837:4028), plus the other Nordic models M HUB groups with the receiver (ids 0x1018,
// 0x1016, 0x102C, 0x102D). Both the receiver and the cable product expose vendor collection
// FF01:0001 (64-byte input/output reports) and answer the same 64-byte "device info" frame:
//   request: output report id 0x4D:
//            [4D][01][flags 01 = read][len 00][cmd lo][cmd hi][biz][seq][xor of bytes 2..7]
//            command 0x0900 = read device info, which carries the battery
//   reply:   input report id 0x4D, same header, then len bytes of data and a checksum:
//            [4D][01][flags][len][cmd lo][cmd hi][biz][seq][data ...][xor of bytes 2..7+len]
//            data[0..1] = vendor id (3837), data[2..3] = paired model id, data[10] = link status
//            (0 = not linked, 1 = connected over the USB cable, 2 = linked to the receiver),
//            data[11] = charge status (1 = charging), data[12] = battery percent, data[13] = game mode
//            With the mouse switched off the receiver still answers, but with len = 1 and a single
//            0xFF in place of the model id (4D 01 01 01 00 09 00 00 FF 00 ...): no battery data, so
//            the device is reported as asleep rather than dropped. Verified on the K7 V2 Ultra+.
// Protocol from MCHOSE's own M HUB web driver (cdn.mchose.com.cn, assets/main-DQZs6ZzX.js:
// getCommandBuffer, mcReadCommand, readDeviceInfo with commandId 0x0900, deviceInfoToObj), and the
// same frame is documented in github.com/HifumiDaisuki01/MchoseBatteryTray (PROTOCOL.md,
// ModernRequest/ParseModern). Confirmed on a real K7 V2 Ultra+ receiver (3837:1014): 29% reported,
// matching M HUB, and on the same mouse plugged in by cable (3837:4028): 25% with charge status 1.
// The G3 V2 driver (mchose_g3.cpp) speaks a different protocol on FF01:0010; this one needs FF01:0001
// and report 0x4D, which the 0x1014 receiver does not expose through the feature-report path.
// K87 V3 keyboard receiver 3837:1015 uses the same command on FF80:0001, with a 20-byte payload
// and paired model 2023. Hardware reports and protocol discovery: HOSTErss, repository PR #2934.
// Keyboard queries are spaced three minutes apart to avoid repeatedly waking an idle keyboard.
#include "drivers.h"

#include <algorithm>
#include <map>
#include <set>

namespace {

const USHORT kVid = 0x3837;
// Receiver ids from M HUB's newMouseWirelessPids; 0x4028 is the K7 V2 Ultra+ on its USB cable.
// Only 0x1014 (receiver) and 0x4028 (cable) were tested, the rest is what M HUB groups with them.
struct Target {
    USHORT pid;
    const wchar_t* via;
    bool keyboard = false;
};
const Target kTargets[] = {
    { 0x1014, L"2.4 GHz receiver" },
    { 0x1018, L"2.4 GHz receiver" },
    { 0x1016, L"2.4 GHz receiver" },
    { 0x102C, L"2.4 GHz receiver" },
    { 0x102D, L"2.4 GHz receiver" },
    { 0x4028, L"USB cable" },
    { 0x1015, L"2.4 GHz receiver", true },
};

const BYTE kReport = 0x4D;
const USHORT kCmdDeviceInfo = 0x0900;
const size_t kData = 8;  // first byte of the data block in both the request and the reply

BYTE Xor(const std::vector<BYTE>& b, size_t from, size_t to) {  // both ends inclusive
    BYTE x = 0;
    for (size_t i = from; i <= to && i < b.size(); i++) x ^= b[i];
    return x;
}

// Returns 1 for a status frame, 0 for a valid frame that says the mouse is not linked, -1 otherwise.
int ParseInfo(const std::vector<BYTE>& r, bool keyboard, int& percent, bool& charging) {
    if (r.size() < kData + 2 || r[0] != kReport) return -1;
    if ((USHORT)(r[4] | (r[5] << 8)) != kCmdDeviceInfo) return -1;
    const size_t len = r[3];
    if (kData + len >= r.size()) return -1;
    // While the mouse is off or asleep the receiver answers with the header only, a single 0xFF in
    // place of the model id and a zeroed data area (4D 01 01 01 00 09 00 00 FF 00 ...), so anything
    // shorter than the 14-byte device-info payload means "not linked" rather than "bad checksum".
    if (len < 14) return keyboard ? (len == 1 && r[kData] == 0xFF ? 0 : -1) : 0;
    if (r[kData + len] != Xor(r, 2, 7 + len)) return -1;
    if ((USHORT)(r[kData] | (r[kData + 1] << 8)) != kVid) return -1;
    if (keyboard && (len < 20 || r[kData + 2] != 0x23 || r[kData + 3] != 0x20)) return -1;
    if (r[kData + 10] == 0) return 0;  // not linked: the mouse is off or asleep
    if (r[kData + 12] > 100) return -1;
    percent = r[kData + 12];
    charging = r[kData + 11] == 1;
    return 1;
}

struct KeyboardPoll {
    ULONGLONG nextAsk = 0;
    DeviceStatus status;
};
std::map<std::wstring, KeyboardPoll> g_keyboards;  // removed when the receiver disappears

int QueryInfo(HidDev& d, const HidColl& c, bool keyboard, int& percent, bool& charging) {
    // One deadline includes writes, retry delays and unrelated input reports, rather than giving
    // each retry a fresh second. This is an active query, so a silent collection gets at most 1 s.
    const ULONGLONG deadline = GetTickCount64() + 1000;
    for (int attempt = 0; attempt < 3; attempt++) {
        int left = (int)((LONGLONG)deadline - (LONGLONG)GetTickCount64());
        if (attempt) {
            if (left <= 150) break;
            Sleep(150);
            left = (int)((LONGLONG)deadline - (LONGLONG)GetTickCount64());
        }
        if (left <= 0) break;
        std::vector<BYTE> req(c.outLen(), 0);
        req[0] = kReport;
        req[1] = 0x01;
        req[2] = 0x01;
        req[4] = (BYTE)(kCmdDeviceInfo & 0xFF);
        req[5] = (BYTE)(kCmdDeviceInfo >> 8);
        req[kData] = Xor(req, 2, 7);
        if (!d.Write(req, std::min(left, 250))) continue;
        const ULONGLONG end = std::min(deadline, GetTickCount64() + 250);
        std::vector<BYTE> r;
        while ((left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64())) > 0) {
            if (d.Read(r, left) < 0) break;
            int state = ParseInfo(r, keyboard, percent, charging);
            if (state >= 0) return state;
        }
    }
    return -1;
}

}  // namespace

void PollMchoseModern(PollContext& ctx) {
    std::set<std::wstring> present;
    for (const HidColl& c : ctx.colls)
        if (c.vid == kVid && c.pid == 0x1015) present.insert(c.path);
    for (auto it = g_keyboards.begin(); it != g_keyboards.end();)
        it = present.count(it->first) ? std::next(it) : g_keyboards.erase(it);
    for (const Target& t : kTargets) {
        if (ctx.IsClaimed(kVid, t.pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, t.pid, [&t](const HidColl& c) {
                 if (t.keyboard)
                     return c.page() == 0xFF80 && c.usage() == 1 && c.outLen() == 64 && c.inLen() == 64;
                 return (c.page() == 0xFF01 || c.page() == 0xFF0B) && c.outLen() >= 64 && c.inLen() >= 21;
             })) {
            if (t.keyboard) {
                auto it = g_keyboards.find(c->path);
                if (it != g_keyboards.end() && GetTickCount64() < it->second.nextAsk) {
                    ctx.Add(it->second.status, kVid, t.pid);
                    break;
                }
            }
            HidDev d;
            if (!d.Open(*c)) continue;
            d.Flush();
            int percent = -1;
            bool charging = false;
            int state = QueryInfo(d, *c, t.keyboard, percent, charging);
            if (state < 0 && !t.keyboard) continue;
            DeviceStatus s;
            s.key = L"mchose:" + Hex4(kVid) + L":" + Hex4(t.pid);
            s.name = HidProduct(*c);
            if (s.name.empty()) s.name = t.keyboard ? L"MCHOSE K87 V3" : L"MCHOSE mouse";
            if (t.keyboard && s.name.size() > 5 && s.name.compare(s.name.size() - 5, 5, L" 2.4G") == 0)
                s.name.resize(s.name.size() - 5);
            s.kind = t.keyboard ? DeviceKind::Keyboard : DeviceKind::Mouse;
            s.via = t.via;
            s.graceSec = 1800;
            if (state == 1) {
                s.percent = Clamp100(percent);
                s.charging = charging;
            } else {
                s.asleep = true;
            }
            if (t.keyboard) g_keyboards[c->path] = { GetTickCount64() + 3 * 60 * 1000, s };
            ctx.Add(s, kVid, t.pid);
            break;
        }
    }
}
