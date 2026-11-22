// ATK / VXE NearLink mice on the "NK mouse NANO dongle" 373B:1216 (VXE R1 NK and siblings). This dongle
// has no 17-byte report 08 collection for the Kysona protocol; it speaks ATK's BITMOUSE protocol on
// vendor collection FF05:0001 instead, so this driver runs before the Kysona driver claims vendor 373B.
//   Output report 08, 64 bytes: [08][checksum][72][02][00][01][cmd][01][00 ...],
//   checksum = sum(bytes 2..63) & 0xFF; target 01 = the mouse behind the dongle.
//   cmd 07 = battery level, cmd 0F = charging flag.
//   Reply on the same collection: [08][72][status][seq][00][cmd][01][value]...; status FF = error.
//   No reply = the mouse is asleep or switched off (moving it brings the reading back).
// From github.com/HeyOkay/HaloBattery PR #350 (providers/atk_bitmouse.py; verified by a VXE R1 owner on
// Windows: the level matches ATK's app, charging follows the cable) and github.com/qwr79/mouse-battery.
//
// ATK Zero uses the same 64-byte framing on FF05:0001, with target 00 for the 1154 cable or 01 for
// the 124F / 1155 8K receivers. Only status 00 is fresh. Command 07 reads the level on either link,
// and command 08 reports charging (02) on the cable. The receivers only relay the battery query.
// Reference: github.com/Fan4Metal/mouse_tray/blob/master/mouse_tray/drivers/chipset/nordic54.py
//
// ATK "8K Nano Dongle" 373B:1178 (NK mouse 8K NANO dongle) is a Holtek receiver with a different protocol on
// vendor collection FF10:0001 (65-byte input/output, 9-byte feature report, no report ids):
//   Feature report 00, 8 bytes: [82][00 x7] (GetConfigData).
//   Reply as an input report: [82][rate][dpi stage][light mode][brightness][speed][color][reserved]
//   [battery][link]...; battery bit 7 = charging, bits 0..6 = percent; link 0 = no mouse paired/awake.
// From ATK HUB (v3-hub.atkgear.com 3.2.33, static/AtkRadioGroup-*.js): the Holtek mouse controller lists
// productId 4472 (0x1178), usagePage 0xFF10 usage 1, "ATK 8K Nano Dongle", and reads battery and link state
// with GetConfigData (0x82) sent as feature report 0, the reply matched on its first byte.
#include "drivers.h"

namespace {

const USHORT kVid = 0x373B;
const USHORT kNanoDongle = 0x1216;
const USHORT kNano8kDongle = 0x1178;
const USHORT kZeroCable = 0x1154;

enum class BitmouseLink { NearLink, ZeroCable, ZeroReceiver };

// Sends command `cmd` and returns its one-byte payload, or -1 when there is no valid reply.
int Query(HidDev& d, BYTE cmd, BitmouseLink link = BitmouseLink::NearLink) {
    const bool zero = link != BitmouseLink::NearLink;
    std::vector<BYTE> req(64, 0);
    req[0] = 0x08;
    req[2] = 0x72;
    req[3] = 0x02;  // request length
    req[5] = link == BitmouseLink::ZeroCable ? 0x00 : 0x01;
    req[6] = cmd;
    req[7] = 0x01;  // expected reply length
    unsigned sum = 0;
    for (size_t i = 2; i < req.size(); i++) sum += req[i];
    req[1] = zero ? 0x7D : (BYTE)(sum & 0xFF);
    d.Flush();
    ULONGLONG end = GetTickCount64() + 500;
    if (!d.Write(req)) return -1;
    if (!zero) end = GetTickCount64() + 700;  // retain the NearLink reply window
    std::vector<BYTE> r;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return -1;
        if (d.Read(r, left) < 0) return -1;
        if (r.size() < 8 || r[0] != 0x08 || r[1] != 0x72) continue;
        if (r[5] != cmd || (zero && r[6] != 0x01)) continue;  // a reply to another request
        if (zero ? r[2] != 0x00 : r[2] == 0xFF) return -1;
        return r[7];
    }
}

void PollZero(PollContext& ctx) {
    bool reported = false;
    // Prefer the cable for a fresh reading while charging and use the same battery history on either link.
    for (USHORT pid : { kZeroCable, (USHORT)0x124F, (USHORT)0x1155 }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF05 && c.usage() == 0x0001 && c.outLen() == 64 && c.inLen() == 64;
             })) {
            ctx.Claim(kVid, pid);  // keep older Kysona frames off this collection, even when asleep
            if (reported) break;
            HidDev d;
            if (!d.Open(*c)) continue;

            const bool wired = pid == kZeroCable;
            const BitmouseLink link = wired ? BitmouseLink::ZeroCable : BitmouseLink::ZeroReceiver;
            const int level = Query(d, 0x07, link);
            if (level < 0 || level > 100) continue;

            DeviceStatus s;
            s.key = L"atkzero:" + Hex4(kZeroCable);
            s.name = L"ATK Zero";
            s.kind = DeviceKind::Mouse;
            s.via = wired ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 1800;
            s.percent = level;
            s.charging = wired && Query(d, 0x08, link) == 0x02;
            ctx.Add(s, kVid, pid);
            reported = true;
            break;
        }
    }
}

// 8K Nano dongle: GetConfigData (0x82) as feature report 0; the reply arrives as an input report
// starting with 0x82. Returns false when nothing valid came back.
bool QueryNano8kConfig(HidDev& d, std::vector<BYTE>& r) {
    d.Flush();
    if (!d.SetFeature({0x00, 0x82})) return false;
    ULONGLONG end = GetTickCount64() + 700;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0) return false;
        if (d.Read(r, left) < 10) return false;
        if (r[0] == 0x82) return true;  // other input reports (e.g. 0x12 DPI / rate notifications) are skipped
    }
}

void PollNano8k(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kNano8kDongle)) return;
    for (const HidColl* c : ctx.Find(kVid, kNano8kDongle, [](const HidColl& c) {
             return c.page() == 0xFF10 && c.usage() == 0x0001 && c.featLen() >= 9 && c.inLen() >= 11;
         })) {
        HidDev d;
        if (!d.Open(*c)) continue;
        std::vector<BYTE> r;
        if (!QueryNano8kConfig(d, r)) continue;
        // The dongle speaks this protocol: keep the Kysona driver from probing it with its 17-byte frames.
        if (r[9] == 0) {
            ctx.Claim(kVid, kNano8kDongle);  // no mouse linked (off or asleep)
            return;
        }
        int level = r[8] & 0x7F;
        if (level < 1 || level > 100) {
            ctx.Claim(kVid, kNano8kDongle);
            return;
        }
        DeviceStatus s;
        s.key = L"atknano8k:" + Hex4(kNano8kDongle);
        s.name = L"ATK 8K Nano mouse";  // the dongle is shared by several ATK / VXE models
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;
        s.percent = level;
        s.charging = (r[8] & 0x80) != 0;
        ctx.Add(s, kVid, kNano8kDongle);
        return;
    }
}

}  // namespace

void PollAtkBitmouse(PollContext& ctx) {
    PollZero(ctx);
    PollNano8k(ctx);
    if (ctx.IsClaimed(kVid, kNanoDongle)) return;
    for (const HidColl* c : ctx.Find(kVid, kNanoDongle, [](const HidColl& c) {
             return c.page() == 0xFF05 && c.usage() == 0x0001 && c.outLen() >= 64 && c.inLen() >= 8;
         })) {
        HidDev d;
        if (!d.Open(*c)) continue;
        int level = Query(d, 0x07);
        if (level < 1 || level > 100) continue;  // asleep / off, or no valid reading
        DeviceStatus s;
        s.key = L"atknk:" + Hex4(kNanoDongle);
        s.name = L"ATK / VXE NK mouse";  // the dongle is shared by several NearLink models
        s.kind = DeviceKind::Mouse;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 1800;  // the dongle stops answering while the mouse sleeps: keep the last value
        s.percent = level;
        s.charging = Query(d, 0x0F) == 1;
        ctx.Add(s, kVid, kNanoDongle);
        break;
    }
}
