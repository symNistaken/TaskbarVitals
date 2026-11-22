// DAREU EM901X receiver (260D:1074) and USB cable (260D:1090), verified on hardware.
// Battery query from the official EM901X AIM-WL driver v1.0 (GetPower / SendCommand); protocol analysis:
// github.com/wushuai0802/myfinder-device-battery/blob/main/engine/upstream-protocols.md
// Unlike the Kysona transport, Feature 08 is sent on FF02:0002 and Input 09 arrives on
// the sibling FF01:0000 collection. Only command 04 is needed; no initialization/settings.
// Reply: [09][04][status=0]...[6=percent][7=charging]. No reply checksum rule is established.
// Sharkoon Skiller SGM50W (260D:1125, issue #1377): Sharkoon's firmware updater for it is a Compx tool
// (DeviceVidPid=260d1124, the ID it flashes over the cable) and the device exposes the same FF02:0002 feature-17 /
// FF01:0000 input-17 pair, so the same query is sent. Not tested on hardware.
//
// DAREU A84 keyboard (260D:0022, SONiX chip). From the official "DAREU A84 THREE MODE MECHANICAL
// KEYBOARD V1.6" driver (dareu.com/pages/keyboard-driver): its launcher (DeviceDriver.exe, config
// `vid="260D" pid="0022"`) opens the device's interface 0 (path containing "mi_00", the keyboard
// collection with the 65-byte feature report) and, on a timer, sends one command only:
//   SetFeature [00][04 F6 00 ...] (65 bytes), then GetFeature report 0, three reads discarded,
//   then up to three reads until byte 15 is non-zero; byte 5 is the level in percent.
// That is the launcher's only HID command, used just for its battery icon. No charging flag is read.
#include "drivers.h"


// DAREU Togran protocol on receivers 260D:1131 and 260D:0042, A980 Pro cable 260D:1148 and EK75 Pro
// cable 260D:0046 (verified on hardware), read from DAREU's own driver (dr.dareu.com, js/device/ohiddevice.js)
// feature report 0A (65 bytes) on consumer-control: [0A][slot<<4][len][class][cmd], reply echoes class/cmd
// slot 0 class 00/A0 lists paired devices, slot n class 07/80 gives [7]=status [8]=level [9]=max, status 1 = charging
static bool TogranQuery(HidDev& dev, BYTE slot, BYTE len, BYTE cls, BYTE cmd, std::vector<BYTE>& r) {
    std::vector<BYTE> req(65, 0);
    req[0] = 0x0A;
    req[1] = (BYTE)(slot << 4);
    req[2] = len;
    req[3] = cls;
    req[4] = cmd;
    if (!dev.SetFeature(req)) return false;
    for (int tries = 0; tries < 15; tries++) {
        Sleep(30);
        if (!dev.GetFeature(0x0A, r) || r.size() < 11) return false;
        if ((r[1] & 0x0F) == 2 && r[3] == cls && r[4] == cmd) return true;
    }
    return false;
}

static void AddTogranBattery(PollContext& ctx, HidDev& dev, BYTE slot, USHORT pid, USHORT devicePid) {
    std::vector<BYTE> b;
    if (!TogranQuery(dev, slot, 4, 0x07, 0x80, b)) return;
    BYTE maxLevel = b[9] ? b[9] : 100;
    if (b[8] > maxLevel) return;
    DeviceStatus s;
    USHORT stablePid = (devicePid == 0x0046 && pid == 0x0046) ? 0x0042 : pid;
    s.key = L"dareu:260D:" + Hex4(stablePid) + (stablePid == devicePid ? L"" : L":" + Hex4(devicePid));
    s.name = devicePid == 0x1148   ? L"DAREU A980 Pro"
             : devicePid == 0x0046 ? L"DAREU EK75 Pro"
                                   : L"DAREU " + Hex4(devicePid);
    s.kind = devicePid == 0x0046 ? DeviceKind::Keyboard : DeviceKind::Mouse;
    s.via = slot ? L"2.4 GHz receiver" : L"USB cable";
    s.percent = (int)b[8] * 100 / maxLevel;
    s.charging = b[7] == 1;
    ctx.Add(s, 0x260D, pid);
    if (devicePid == 0x0046 && pid == 0x0046) ctx.Claim(0x260D, 0x0042);
}

// receivers 1131 and 0042 list paired devices by slot, cables answer on slot 0.
static void PollDareuTogran(PollContext& ctx) {
    for (USHORT pid : { USHORT(0x1131), USHORT(0x0046), USHORT(0x0042), USHORT(0x1148) }) {
        if (ctx.IsClaimed(0x260D, pid)) continue;
        auto colls = ctx.Find(0x260D, pid, [](const HidColl& c) {
            return c.page() == 0x000C && c.usage() == 1 && c.featLen() == 65;
        });
        if (colls.size() != 1) continue;
        HidDev dev;
        if (!dev.OpenFeatureOnly(*colls[0])) continue;
        if (pid == 0x1148 || pid == 0x0046) {
            AddTogranBattery(ctx, dev, 0, pid, pid);
            continue;
        }
        std::vector<BYTE> r;
        if (!TogranQuery(dev, 0, 6, 0x00, 0xA0, r)) continue;
        int count = r[7];
        for (int slot = 1; slot <= count && slot <= 5; slot++) {
            const BYTE* e = &r[8 + (slot - 1) * 3];
            if (e[0]) AddTogranBattery(ctx, dev, (BYTE)slot, pid, (USHORT)((e[1] << 8) | e[2]));
        }
    }
}

namespace {

void PollDareuA84(PollContext& ctx) {
    const USHORT vid = 0x260D, pid = 0x0022;
    if (ctx.IsClaimed(vid, pid)) return;
    auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
        return c.iface == 0 && c.page() == 0x01 && c.usage() == 0x06 && c.featLen() == 65;
    });
    if (colls.size() != 1) return;
    HidDev d;
    if (!d.OpenFeatureOnly(*colls[0])) return;
    std::vector<BYTE> req(65, 0), r;
    req[1] = 0x04;
    req[2] = 0xF6;
    if (!d.SetFeature(req)) return;
    Sleep(10);
    for (int i = 0; i < 3; i++) {  // the official driver discards these reads
        d.GetFeature(0, r);
        Sleep(10);
    }
    for (int i = 0; i < 3; i++) {
        if (i) Sleep(10);
        if (!d.GetFeature(0, r) || r.size() < 16 || r[15] == 0) continue;
        if (r[5] > 100) return;
        DeviceStatus s;
        s.key = L"dareu:" + Hex4(vid) + L":" + Hex4(pid);
        s.name = L"DAREU A84";
        s.kind = DeviceKind::Keyboard;
        s.via = L"HID";
        s.percent = r[5];
        ctx.Add(s, vid, pid);
        return;
    }
}

// DAREU A950 Wing: USB cable 260D:1196, 2.4 GHz 8K receiver 260D:1154 (verified on hardware).
// Compx 17-byte output report on FF02:0002; reply gives percent, charging flag and millivolts.
void PollDareuA950Wing(PollContext& ctx) {
    const USHORT vid = 0x260D;
    const USHORT kCablePid = 0x1196;
    const USHORT kReceiverPid = 0x1154;
    // Cable comes before receiver; an answering cable claims the receiver so the mouse is listed once.
    for (USHORT pid : { kCablePid, kReceiverPid }) {
        if (ctx.IsClaimed(vid, pid)) continue;
        auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.iface == 1 && c.page() == 0xFF02 && c.usage() == 2 && c.inLen() == 17 && c.outLen() == 17;
        });
        if (colls.size() != 1) continue;

        HidDev dev;
        if (!dev.Open(*colls[0])) continue;
        std::vector<BYTE> req(17, 0), r;
        req[0] = 0x08;
        req[1] = 0x04;
        req[16] = 0x49;
        dev.Flush();
        if (!dev.Write(req)) continue;
        ULONGLONG end = GetTickCount64() + 1000;
        for (int reports = 0; reports < 32; reports++) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0 || dev.Read(r, left) < 0) break;
            if (r.size() != 17 || r[0] != 0x08 || r[1] != 0x04 || r[6] > 100) continue;
            BYTE sum = 0;
            for (BYTE b : r) sum += b;
            if (sum != 0x55) continue;
            DeviceStatus s;
            s.key = L"dareu:260D:" + Hex4(kReceiverPid);
            s.name = L"DAREU A950 Wing";
            s.kind = DeviceKind::Mouse;
            s.via = pid == kCablePid ? L"USB cable" : L"2.4 GHz receiver";
            s.percent = r[6];
            s.charging = r[7] != 0;
            s.millivolts = (r[8] << 8) | r[9];
            s.graceSec = 1800;
            ctx.Add(s, vid, pid);
            if (pid == kCablePid) ctx.Claim(vid, kReceiverPid);
            break;
        }
    }
}

}  // namespace

void PollDareu(PollContext& ctx) {
    PollDareuTogran(ctx);
    PollDareuA84(ctx);
    PollDareuA950Wing(ctx);
    const USHORT vid = 0x260D;
    // Prefer a valid cable reading over potentially cached receiver telemetry.
    bool em901xRead = false;
    for (USHORT pid : { USHORT(0x1090), USHORT(0x1074), USHORT(0x1125) }) {
        if (ctx.IsClaimed(vid, pid) || (em901xRead && pid == 0x1074)) continue;
        auto inputs = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.iface == 1 && c.page() == 0xFF01 && c.usage() == 0 && c.inLen() == 17;
        });
        auto features = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.iface == 1 && c.page() == 0xFF02 && c.usage() == 2 && c.featLen() == 17;
        });
        // Do not pair arbitrary collections when multiple devices share a product ID.
        if (inputs.size() != 1 || features.size() != 1) continue;

        HidDev input, feature;
        if (!input.Open(*inputs[0]) || !feature.OpenFeatureOnly(*features[0])) continue;
        std::vector<BYTE> req(17, 0), r;
        req[0] = 0x08;
        req[1] = 0x04;
        req[16] = 0x49;  // 0x55 minus the sum of bytes 0..15
        input.Flush();
        if (!feature.SetFeature(req)) continue;
        ULONGLONG end = GetTickCount64() + 1000;
        for (int reports = 0; reports < 32; reports++) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0 || input.Read(r, left) < 0) break;
            if (r.size() != 17 || r[0] != 0x09 || r[1] != 0x04 || r[2] != 0 || r[6] > 100) continue;
            DeviceStatus s;
            s.key = L"dareu:" + Hex4(vid) + L":" + Hex4(pid);
            s.name = pid == 0x1125 ? L"Sharkoon Skiller SGM50W" : L"DAREU EM901X";
            s.kind = DeviceKind::Mouse;
            s.via = pid == 0x1090 ? L"USB cable" : L"2.4 GHz receiver";
            s.percent = r[6];
            s.charging = r[7] != 0;
            ctx.Add(s, vid, pid);
            em901xRead = pid != 0x1125;
            break;
        }
    }
}
