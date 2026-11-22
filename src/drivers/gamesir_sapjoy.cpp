// GameSir Tarantula Pro / NovaPro (Super Nova, T4NPro) — battery over the SAPJoy "device info" frame.
//
// These pads show up as a wired Xbox controller to XInput, so the level is read from their vendor
// collection (usage page 0xFFA0, output report 0x07 / 32 bytes, input report 0x06 / 64 bytes, no
// feature channel) with the read-only GetDeviceInfo query GameSir's own client sends:
//   Request: 07 03 0A 01, zero padded to 32 bytes
//   Reply:   [0]=06 [1]=len [2]=0A [3]=01 [4]=id [5..6]=firmware [7..12]=MAC
//            [13]=battery (0..100 percent, FF = charging) [14]=type
//
// Protocol from the captured traffic of GameSir's client on a NovaPro (Super Nova / T4NPro) and its
// main.js decoder (chargeState = 255 == battery, battery clamped at 100). Confirmed on real
// hardware: the pad reported 0x60 (96%) while GameSir's own UI showed 96%.
//
// 3537:103E is shared by the Tarantula Pro and the NovaPro on their receivers, and the two answer
// this same frame, so the product string is used for the name instead of a fixed one.
// 3537:100E is the NovaPro on a cable: same frame, and the pad reports a level there too.
#include "drivers.h"
#include "gamesir_sapjoy_protocol.h"
#include <algorithm>

namespace {

const USHORT kGameSirVid = 0x3537;

// Cable and receiver ids that answer the SAPJoy frame. Nova / Nova 2 / Nova 2 Lite (0x1098, 0x100F,
// 0x1040) and Cyclone 2 (0x100B) are handled by the drivers above and are deliberately not listed.
std::wstring Identity(const GameSirSapjoy::Reading& r, USHORT pid) {
    // Only deduplicate links with the same concrete address, never just the shared model ID.
    bool zero = std::all_of(r.address.begin(), r.address.end(), [](BYTE b) { return b == 0; });
    bool erased = std::all_of(r.address.begin(), r.address.end(), [](BYTE b) { return b == 0xFF; });
    if (zero || erased) return L"gamesir:" + Hex4(pid);
    std::wstring key = L"gamesir:sapjoy:";
    for (BYTE b : r.address) key += Hex4(b).substr(2);
    return key;
}

}  // namespace

void PollGameSirSapjoy(PollContext& ctx) {
    const auto first = ctx.out.size();
    const USHORT vid = kGameSirVid;
    for (USHORT pid : {USHORT(0x100E), USHORT(0x103E), USHORT(0x1050)}) {
        if (ctx.IsClaimed(vid, pid)) continue;
        // 0xFFA0 is the page the vendor library hard-checks; the keyboard/gamepad pages either time
        // out or refuse the write.
        for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
                 return c.page() == 0xFFA0 && c.outLen() == 32 && c.inLen() == 64 && c.featLen() == 0;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            d.Flush();

            BYTE req[GameSirSapjoy::kOutputReportLen];
            if (!GameSirSapjoy::BuildDeviceInfoRequest(req, sizeof(req))) continue;
            if (!d.Write(std::vector<BYTE>(req, req + sizeof(req)))) continue;

            std::vector<BYTE> r;
            GameSirSapjoy::Reading reading;
            const ULONGLONG end = GetTickCount64() + 900;
            for (;;) {
                int left = static_cast<int>(static_cast<LONGLONG>(end) - static_cast<LONGLONG>(GetTickCount64()));
                if (left <= 0) break;
                int n = d.Read(r, std::min(left, 200));
                if (n < 0) break;
                if (n < 15) continue;   // state frames are shorter or use another header
                reading = GameSirSapjoy::DecodeDeviceInfo(r.data(), r.size());
                if (reading.answered) break;
            }
            if (!reading.answered) continue;

            DeviceStatus s;
            s.key = Identity(reading, pid);
            std::wstring product = HidProduct(*c);
            s.name = !product.empty() ? product : L"GameSir controller";
            s.kind = DeviceKind::Gamepad;
            // Prefer a valid cable reading when the same address answers over both links.
            s.via = pid == 0x100E ? L"USB cable" : L"2.4 GHz receiver";
            s.charging = reading.charging;
            if (!reading.charging && reading.percent >= 0) s.percent = reading.percent;
            auto duplicate = std::find_if(ctx.out.begin() + first, ctx.out.end(),
                [&](const DeviceStatus& other) { return other.key == s.key; });
            if (duplicate == ctx.out.end()) ctx.Add(s, vid, pid);
            else ctx.Claim(vid, pid);
            break;
        }
    }
}
