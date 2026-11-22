// Jabra headsets that speak Jabra's GN Protocol (GNP), on their own USB cable or behind a Jabra Link
// dongle. GNP runs on the vendor collection: usage page FF00, usage 05 (Link 390, Evolve3) or 01 (Link
// 380, Evolve2), report 05, 64 bytes. Instead of listing product ids, every Jabra product with that
// collection is asked; what differs between products is worked out per device.
//   query: [05][dst][src 00][seq][40|len][class][op][payload]     len = bytes after the report id
//   reply: [05][dst 00][src][seq][C0|len][class][op][payload]     class FE = NAK, op = reason
// Addresses: 08 = a headset on its own USB cable, 01 = a Link dongle, 04 = the headset behind it (from
// Jabra Direct's FWU/DeviceInfo.xml). Link dongles, by their USB product string, are asked at 04 first,
// everything else at 08 first; the address that answers is kept per product.
// Battery = class 12 / op 02 (STATUS__HS_BATTERY), reply payload [flags][level]; flags bit 0 = charging,
// bit 7 = per-unit levels follow (earbuds), the level byte is then the combined value.
// Name = class 02 / op 00, reply payload [n][n bytes UTF-8].
// A switched-off headset behind a dongle gets a NAK (F3, peer offline) while the dongle stays present.
// DECT bases are left out: their base answers at 08 too, as the base, with the headset at 10.
// Protocol from github.com/Watchdog0x/jabridge (cmd/jabridge/jabraApi.go) and HeadsetControl's GNP
// driver; verified on a Link 390 (0B0E:2E51) with an Evolve3 75, and on the Evolve3 75's own USB
// cable (0B0E:2548).
#include "drivers.h"

#include <algorithm>
#include <iterator>
#include <map>

namespace {

constexpr USHORT kJabra = 0x0B0E;
constexpr BYTE kDongleHeadset = 0x04, kUsbHeadset = 0x08;
constexpr BYTE kNakPeerOffline = 0xF3;

// DECT bases and dongles (Engage 65 / 75 incl. SE, PRO 9xx, GO 6400, Link 400, NXT UC-7500, H40),
// from Jabra Direct's DeviceInfo.xml; sorted for binary_search.
constexpr USHORT kDectBases[] = {
    0x1000, 0x1016, 0x1017, 0x1018, 0x1021, 0x1022, 0x1023, 0x1037, 0x1038, 0x1039, 0x1041, 0x1042,
    0x1043, 0x1110, 0x1112, 0x1113, 0x1118, 0x111A, 0x111B, 0x111D, 0x111E, 0x111F, 0x1120, 0x1121,
    0x1122, 0x1123, 0x1124, 0x1126, 0x1131, 0x1132, 0x1133, 0x1134, 0x1135, 0x1136, 0x1140, 0x1141,
    0x1142, 0x1146, 0x1147, 0x1148, 0x114A, 0x114B, 0x114C, 0x114D, 0x114E, 0x114F, 0x1150, 0x1151,
    0xA700, 0xA701, 0xA702, 0xA703, 0xA704, 0xA705, 0xA706, 0xAE60, 0xAE62, 0xAE63,
};

bool IsDectBase(USHORT pid) { return std::binary_search(std::begin(kDectBases), std::end(kDectBases), pid); }

enum class Reply { Ok, Nak, None };

// On Ok `payload` holds the reply payload, on Nak the one-byte reason.
Reply Query(HidDev& d, BYTE dst, BYTE cls, BYTE op, std::vector<BYTE>& payload, int timeoutMs) {
    static BYTE seq = 0x20;
    BYTE s = seq++;
    d.Flush();
    if (!d.Write({ 0x05, dst, 0x00, s, 0x46, cls, op }, 300)) return Reply::None;
    ULONGLONG end = GetTickCount64() + timeoutMs;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        int n = d.Read(r, left);
        if (n < 0) return Reply::None;
        if (n < 7) continue;  // unrelated short report
        if (r[0] != 0x05 || r[1] != 0x00 || r[2] != dst || r[3] != s || (r[4] & 0xC0) != 0xC0) continue;
        if (r[5] == 0xFE) {
            payload.assign(1, r[6]);
            return Reply::Nak;
        }
        int len = r[4] & 0x3F;  // counts from r[1]
        if (r[5] != cls || r[6] != op || len < 6 || len + 1 > (int)r.size()) continue;
        payload.assign(r.begin() + 7, r.begin() + len + 1);
        return Reply::Ok;
    }
    return Reply::None;
}

std::wstring HeadsetName(HidDev& d, BYTE address) {
    std::vector<BYTE> p;
    if (Query(d, address, 0x02, 0x00, p, 400) != Reply::Ok || p.empty() || p[0] == 0 || p[0] + 1u > p.size()) return {};
    return Utf8ToWide(std::string((const char*)&p[1], p[0]));
}

}  // namespace

void PollJabra(PollContext& ctx) {
    // Headset address per product, kept once a headset answered there.
    static std::map<USHORT, BYTE> addresses;
    // Products not asked again before this tick: for good when every collection NAKed the battery
    // query at both addresses (wired headsets), for a while when nothing answered at all.
    static std::map<USHORT, ULONGLONG> skipUntil;
    constexpr ULONGLONG kSilentRetryMs = 5 * 60 * 1000;
    const ULONGLONG now = GetTickCount64();
    // Headset name per product: asked once while connected (a failed ask caches the generic name),
    // forgotten when the headset goes offline so a newly paired headset is picked up.
    static std::map<USHORT, std::wstring> names;

    for (auto [vid, pid] : ctx.Products(kJabra)) {
        if (IsDectBase(pid)) continue;
        if (auto skip = skipUntil.find(pid); skip != skipUntil.end() && now < skip->second) continue;
        auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
            return c.page() == 0xFF00 && (c.usage() == 0x05 || c.usage() == 0x01) && c.outLen() >= 64 && c.inLen() >= 64;
        });
        // Products with both collections (Evolve3 75 on USB) answer on 05.
        std::stable_partition(colls.begin(), colls.end(), [](const HidColl* c) { return c->usage() == 0x05; });
        // Decided over all collections: whether a full probe ran and found nothing, and whether
        // every reply was a NAK. Dongles and known addresses are never skipped.
        bool reported = false, probed = false, skippable = true, allNak = true;
        for (const HidColl* c : colls) {
            HidDev d;
            if (!d.Open(*c)) continue;
            auto known = addresses.find(pid);
            const bool dongle = known != addresses.end() ? known->second == kDongleHeadset
                                                         : HidProduct(*c).find(L"Link") != std::wstring::npos;
            std::vector<BYTE> order;
            if (known != addresses.end()) order = { known->second };
            else if (dongle) order = { kDongleHeadset, kUsbHeadset };
            else order = { kUsbHeadset, kDongleHeadset };

            std::vector<BYTE> p;
            BYTE address = 0;
            bool offline = false;
            if (dongle || order.size() == 1) skippable = false;
            for (BYTE a : order) {
                Reply rep = Query(d, a, 0x12, 0x02, p, 700);
                if (rep == Reply::Ok && p.size() >= 2) {
                    address = a;
                    break;
                }
                // A dongle reports its headset offline rather than absent (F3); on other products
                // any other NAK means there is no headset with a battery at that address.
                if (rep == Reply::Nak && a == kDongleHeadset && (dongle || p[0] == kNakPeerOffline)) {
                    address = a;
                    offline = true;
                    break;
                }
                if (rep != Reply::Nak) allNak = false;
            }
            if (!address) {
                addresses.erase(pid);
                probed = true;
                continue;
            }
            addresses[pid] = address;

            DeviceStatus s;
            s.key = L"jabra:" + Hex4(pid);
            s.kind = DeviceKind::Headset;
            s.via = address == kDongleHeadset ? L"Jabra Link dongle" : L"USB cable";
            if (!offline) {
                s.percent = p[1] <= 100 ? p[1] : -1;
                s.charging = (p[0] & 0x01) != 0;
                if (!names.count(pid)) {
                    std::wstring n = HeadsetName(d, address);
                    names[pid] = n.empty() ? L"Jabra headset" : n;
                }
                s.name = names[pid];
            } else {
                s.connected = false;  // dongle present, headset off or out of range
                auto it = names.find(pid);
                s.name = it != names.end() ? it->second : L"Jabra headset";
                names.erase(pid);
            }
            ctx.Add(s, kJabra, pid);
            reported = true;
            break;
        }
        if (!reported && probed && skippable) skipUntil[pid] = allNak ? ~0ULL : now + kSilentRetryMs;
    }
}
