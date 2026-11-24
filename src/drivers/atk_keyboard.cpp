// Evision keyboards on the shared 2.4 GHz receiver (320F:5088) or USB cable (320F:5055): ATK V75X and the
// VGN / VXE keyboards that VGN HUB drives the same way.
// ATK V75X: ported from HaloBattery (github.com/HeyOkay/HaloBattery, providers/atk_keyboard.py, MIT; confirmed
// there by its author on both), from ATK HUB 3.2.29's Evision keyboard driver (keyboard-info.util-*.js).
// VGN / VXE: VGN HUB (drive.vgnlab.com.cn, assets/index-*.js) lists these product names on 320F:5088 / 5055,
// FF1C:0092, report 4, type "weisheng"; its getElectricQuantity() sends [04][00 00][1A][06] and reads the
// level from the same byte, and it checks that the reply echoes the first bytes of the request. myfinder-device-
// battery (engine/upstream-protocols.md, "VGN 2.4G keyboard receiver") read 93% this way on a VGN receiver
// (VGN HUB showed 94%) and saw the reply arrive about 1 s after the request, so up to 2.5 s is waited.
//   Output report 04 on FF1C:0092, 31 payload bytes: [04][seq lo][seq hi][1A][00, or 06 for VGN][00 ...]
//   Input report 04: [04][seq lo][seq hi][1A][..][00 00][..][percent][state: 0 on battery, 1/2 charging]
// These ids are shared by many Evision keyboards, so the product name is checked as well, and the keyboard is
// named after it (the receiver's name without "2.4G Dongle").
// The receiver is also sold under the generic name "Wireless Gaming Keyboard" (Feker Alice80 and others); the same
// request and reply were confirmed on a Feker Alice80 on that receiver (seq echoed, command 1A, level at byte 8),
// and its backlight still timed out while it was asked every minute. Such keyboards are shown under that name.
// HaloBattery only asks over the receiver while the keyboard is being typed on, so a sleeping keyboard is
// not woken. This app does not watch keyboard input, so it asks at most every 10 minutes over the receiver
// instead (every poll on the cable, which powers the keyboard anyway).
#include "drivers.h"

#include <map>

namespace {

constexpr USHORT kVid = 0x320F, kReceiver = 0x5088, kCable = 0x5055;
constexpr ULONGLONG kReceiverIntervalMs = 10 * 60 * 1000;

struct Model {
    const wchar_t* product;  // product string of the receiver or the cable
    bool vgn;                // asked the way VGN HUB asks
};
const Model kModels[] = {
    { L"ATK V75X 2.4G Dongle", false },     { L"ATK V75X", false },
    { L"VGN S99 2.4G Dongle", true },       { L"VGN S99", true },
    { L"VGN V98pro V2 2.4G Dongle", true }, { L"VGN V98pro V2", true },
    { L"VGN V98 PRO 2.4G Dongle", true },   { L"VGN V98pro", true },
    { L"VGN N75 PRO 2.4G Dongle", true },   { L"VGN N75 PRO", true },
    { L"VGN VXE75 V2 2.4G Dongle", true },  { L"VGN VXE75 V2", true },
    { L"VGN VXE75 2.4G Dongle", true },     { L"VGN VXE75", true },
    { L"VXE V87 2.4G Dongle", true },       { L"VXE V87", true },
    { L"VXE V87 RGB", true },
    { L"Wireless Gaming Keyboard", false },  // generic receiver name (Feker Alice80 and others)
    // Wobkey Rainy 75, first hardware: the same Evision controller under Wobkey's name, on the
    // cable id (320F:5055) rather than the 36B0 ids the Rainy75 V2 driver expects. It answered
    // 0x1A with the level in r[8] on hardware while plugged in.
    { L"Rainy 75", false },
};

const Model* FindModel(std::wstring& name) {
    while (!name.empty() && name.back() == L' ') name.pop_back();
    for (const Model& m : kModels)
        if (_wcsicmp(name.c_str(), m.product) == 0) return &m;
    return nullptr;
}

std::wstring KeyboardName(const std::wstring& product) {
    const size_t n = wcslen(L" 2.4G Dongle");
    if (product.size() > n && _wcsicmp(product.c_str() + product.size() - n, L" 2.4G Dongle") == 0)
        return product.substr(0, product.size() - n);
    return product;
}

bool ReadBattery(const HidColl& c, bool vgn, int& percent, bool& charging) {
    static USHORT seq = 0;
    seq = (USHORT)(seq % 0xFFFF + 1);
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    std::vector<BYTE> req(32, 0);
    req[0] = 0x04;
    req[1] = (BYTE)(seq & 0xFF);
    req[2] = (BYTE)(seq >> 8);
    req[3] = 0x1A;
    req[4] = vgn ? 0x06 : 0x00;
    if (!d.Write(req)) return false;
    ULONGLONG end = GetTickCount64() + 2500;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int n = d.Read(r, 100);
        if (n < 10 || r[0] != 0x04 || r[1] != (BYTE)(seq & 0xFF) || r[2] != (BYTE)(seq >> 8) || r[3] != 0x1A || r[5] || r[6])
            continue;
        if (r[8] > 100 || r[9] > 2) return false;
        percent = r[8];
        charging = r[9] == 1 || r[9] == 2;
        return true;
    }
    return false;
}

constexpr USHORT kZ87Vid = 0x373B, kZ87Receiver = 0x1012;
bool ReadZ87Battery(const HidColl& c, USHORT& seq, int& percent, bool& charging) {
    seq = (USHORT)(seq % 0xFFFF + 1);
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    std::vector<BYTE> req(32, 0);  // HidDev pads to the output report length (64 on the Z87).
    req[0] = 0x04;
    req[1] = (BYTE)(seq & 0xFF);
    req[2] = (BYTE)(seq >> 8);
    req[3] = 0x1A;
    ULONGLONG end = GetTickCount64() + 1000;
    if (!d.Write(req)) return false;
    std::vector<BYTE> r;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= end) return false;
        int n = d.Read(r, (int)(end - now));
        if (n < 0) return false;
        if (n < 4 || r[0] != 0x04 || r[1] != req[1] || r[2] != req[2]) continue;
        if (c.vid == kZ87Vid && r[3] == 0xFF) return false;  // sleeping keyboard
        if (n < 10 || r[3] != 0x1A || r[5] || r[6]) continue;
        if (r[8] > 100 || r[9] > 2) return false;
        percent = r[8];
        charging = r[9] == 1 || r[9] == 2;
        return true;
    }
}


// Keep the Z87 cache separate from the existing VGN/VXE/Feker/V75X path.
void PollZ87(PollContext& ctx) {
    struct Cache { USHORT sequence=0; ULONGLONG nextAsk=0; int percent=-1; bool charging=false; };
    static Cache cache;
    if (ctx.IsClaimed(kZ87Vid, kZ87Receiver)) return;
    for (const HidColl* c : ctx.Find(kZ87Vid, kZ87Receiver, [](const HidColl& c) {
        return c.page()==0xFF1C && c.usage()==0x0092 && c.inLen()==64 && c.outLen()==64;
    })) {
        std::wstring name=HidProduct(*c);
        while (!name.empty() && name.back()==L' ') name.pop_back();
        if (_wcsicmp(name.c_str(),L"ATK Z87 Dongle")!=0) continue;
        ULONGLONG now=GetTickCount64();
        if (now>=cache.nextAsk) {
            cache.nextAsk=now+kReceiverIntervalMs;
            if (!ReadZ87Battery(*c,cache.sequence,cache.percent,cache.charging)) {
                cache.percent=-1; cache.charging=false;
                cache.nextAsk=now+30*1000;
            }
        }
        DeviceStatus s;
        s.key=L"atk_keyboard:1012"; s.name=L"ATK Z87";
        s.kind=DeviceKind::Keyboard; s.via=L"2.4 GHz receiver";
        s.graceSec=1800; s.percent=cache.percent; s.charging=cache.charging;
        s.asleep=s.percent<0;
        ctx.Add(s,kZ87Vid,kZ87Receiver);return;
    }
    cache={}; // Reconnects should not inherit an old receiver reading.
}


}  // namespace

void PollAtkKeyboard(PollContext& ctx) {
    PollZ87(ctx);
    static ULONGLONG lastAsk = 0;
    static int lastPercent = -1;
    static bool lastCharging = false;
    for (USHORT pid : { kCable, kReceiver }) {  // the cable wins when both are connected
        if (ctx.IsClaimed(kVid, pid)) continue;
        bool cable = pid == kCable;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) { return c.page() == 0xFF1C && c.usage() == 0x0092; })) {
            std::wstring product = HidProduct(*c);
            const Model* model = FindModel(product);
            if (!model) continue;
            DeviceStatus s;
            s.key = L"atk_keyboard:5088";  // one entry for the keyboard on either connection
            s.name = KeyboardName(model->product);
            s.kind = DeviceKind::Keyboard;
            s.via = cable ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 1800;
            ULONGLONG now = GetTickCount64();
            bool ask = cable || !lastAsk || now - lastAsk >= kReceiverIntervalMs;
            int percent = -1;
            bool charging = false;
            if (ask) {
                lastAsk = now;
                if (ReadBattery(*c, model->vgn, percent, charging)) {
                    lastPercent = percent;
                    lastCharging = charging;
                }
            }
            if (ask && percent < 0) {
                s.asleep = true;  // no reply: asleep, off or busy; keep the last value
            } else {
                s.percent = ask ? percent : lastPercent;
                s.charging = ask ? charging : lastCharging;
                if (s.percent < 0) s.asleep = true;
            }
            ctx.Add(s, kVid, pid);
            if (cable) ctx.Claim(kVid, kReceiver);
            return;
        }
    }
}
