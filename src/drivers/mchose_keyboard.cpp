// MCHOSE keyboards on the current common 2.4 GHz receiver, VID 3837 PID 3033 (product string
// "MCHOSE <model> 2.4G"; the G75 V2, G87 V2, K99 V3 and K87S all ship with it). M HUB pairs this
// receiver with two different protocols depending on the keyboard: the G75 V2 / G87 V2 / K99 V3
// speak "GLW" frames on the id-less 64-byte collection 0001:0000 (the FF31:0074 collection answers
// the same frames as a fallback), while the K87S speaks the "QHW" report-0x4D protocol behind
// different report descriptors. This driver implements GLW only: a QHW receiver never answers
// these frames, so it stays hidden instead of showing a phantom entry.
//   request: 64-byte output report without a report id:
//            [0]=0x55 flag [1]=command [2]=0x00 [3]=checksum [4]=size [5]=offset lo [6]=offset hi
//            [7]=0x00, zero padded; checksum = (size + offset + payload) & 0xFF, reads have no payload
//   reply:   input report with the same header, flag 0xAA, payload from byte 8 on
//   battery: "func config" read (command 0x05, offset 0, size 56): payload[32] bit 7 = charging,
//            bits 0..6 = battery percent (85 % while discharging on the test G75 V2)
// Protocol from MCHOSE's own M HUB web driver (cdn.mchose.com.cn customPage bundle, "Channel:Keyboard"
// GLW class: _simpleGetCommand / getFuncConfig / parseBaseFuncConfigStruct), confirmed on a real
// MCHOSE G75 V2 receiver (3837:3033): getInfo answers with a build-date string ("Jan 12 2026..."
// "...2.4G Dongle"), the func-config checksum matches byte for byte, and the battery byte follows
// the keyboard. The battery pushes (FA FB 07 <percent> <charge>) that M HUB also listens for were
// never observed on this receiver, so the value is polled instead.
// Like the ATK keyboard, the receiver is asked at most every 10 minutes rather than every poll:
// the keyboard decides on its own when to sleep, and the app should not keep it awake.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x3837;
constexpr USHORT kPid = 0x3033;
constexpr BYTE kFlag = 0x55;          // request flag; replies use 0xAA
constexpr BYTE kCmdFuncConfig = 0x05;
constexpr size_t kPayload = 8;        // payload starts at byte 8 of the reply
constexpr size_t kBattery = 32;       // battery byte inside the func-config payload
constexpr ULONGLONG kAskIntervalMs = 10 * 60 * 1000;

std::wstring DisplayName(const HidColl& c) {
    std::wstring name = HidProduct(c);
    const wchar_t* suffix = L" 2.4G";  // "MCHOSE G75 V2 2.4G" -> "MCHOSE G75 V2"
    const size_t n = wcslen(suffix);
    if (name.size() > n && name.compare(name.size() - n, n, suffix) == 0) name.resize(name.size() - n);
    return name.empty() ? std::wstring(L"MCHOSE keyboard") : name;
}

bool ParseFuncConfig(const std::vector<BYTE>& r, int& percent, bool& charging) {
    if (r.size() < kPayload + kBattery + 1) return false;
    if (r[0] != 0xAA || r[1] != kCmdFuncConfig || r[5] || r[6]) return false;  // offset 0
    size_t size = r[4];
    if (size < kBattery + 1 || kPayload + size > r.size()) return false;
    int sum = r[4] + r[5] + r[6] + r[7];
    for (size_t i = 0; i < size; i++) sum += r[kPayload + i];
    if ((sum & 0xFF) != r[3]) return false;
    percent = r[kPayload + kBattery] & 0x7F;
    charging = (r[kPayload + kBattery] >> 7) != 0;
    return percent <= 100;
}

bool QueryBattery(const HidColl& c, int& percent, bool& charging) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt) Sleep(150);
        // Read command, 56 bytes at offset 0: [report id 0] flag, command, 0x00, checksum, size, offset.
        if (!d.Write({ 0x00, kFlag, kCmdFuncConfig, 0x00, 56, 56, 0x00, 0x00, 0x00 })) continue;
        ULONGLONG end = GetTickCount64() + 500;
        std::vector<BYTE> r;
        while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (d.Read(r, left) < 0) break;
            if (ParseFuncConfig(r, percent, charging)) return true;
        }
    }
    return false;
}

}  // namespace

void PollMchoseKeyboard(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    static ULONGLONG lastAsk = 0;
    static int lastPercent = -1;
    static bool lastCharging = false;

    std::vector<const HidColl*> cs = ctx.Find(kVid, kPid, [](const HidColl& c) {
        return c.inLen() == 65 && c.outLen() == 65 &&
               ((c.page() == 0x0001 && c.usage() == 0x0000) ||  // M HUB's filter for this receiver
                (c.page() == 0xFF31 && c.usage() == 0x0074));   // answers the same frames
    });
    if (cs.empty()) return;  // receiver not present

    const ULONGLONG now = GetTickCount64();
    const bool ask = !lastAsk || now - lastAsk >= kAskIntervalMs;
    int percent = -1;
    bool charging = false;
    if (ask) {
        lastAsk = now;
        for (const HidColl* c : cs) {
            if (QueryBattery(*c, percent, charging)) break;
        }
    }
    if (percent < 0 && !ask) {  // between asks: keep showing the previous reading
        percent = lastPercent;
        charging = lastCharging;
    }
    if (percent < 0 && lastPercent < 0) return;  // never answered: QHW receiver (K87S) or dead link
    if (percent >= 0) {
        lastPercent = percent;
        lastCharging = charging;
    }

    DeviceStatus s;
    s.key = L"mchose:3837:3033";
    s.name = DisplayName(*cs.front());
    s.kind = DeviceKind::Keyboard;
    s.via = L"2.4 GHz receiver";
    s.graceSec = 1800;
    if (percent >= 0) {
        s.percent = Clamp100(percent);
        s.charging = charging;
    } else {
        s.asleep = true;  // asked but no reply: keyboard off, asleep or busy; keep the last value
    }
    ctx.Add(s, kVid, kPid);
}
