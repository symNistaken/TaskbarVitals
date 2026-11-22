// Apple AirPods / Beats (AirPods Max, AirPods Pro 1/2/3 and AirPods 1-5) over Bluetooth.
//
// Windows does not read the battery of AirPods (they report it through Apple-specific HFP commands),
// so the Bluetooth fallback shows nothing for them. They do broadcast it in clear, several times a
// second, in the BLE "proximity pairing" message iPhones use for their battery popup: manufacturer
// data of company 0x004C (Apple), type 0x07, length 0x19:
//   [0] 07  [1] 19  [2] 01 (paired status mode; 00 is pairing mode)
//   [3..4] model (LE, = Bluetooth PID, e.g. 0x200A AirPods Max, 0x2014 AirPods Pro 2)
//   [5] status: bit 5 set = the left bud is the one broadcasting (dual-bud models)
//   [6] dual-bud: low nibble = broadcasting bud, high nibble = the other one (0-10 = x10 %, 15 = unknown)
//       single-battery (AirPods Max, Beats headphones): low nibble = battery (0-10 = x10 %), high nibble = 0
//   [7] high nibble: bit 0 broadcasting/single charging, bit 1 other bud charging, bit 2 case charging
//       (bit 3 is always set on AirPods Max: 0x80 discharging, 0x90 charging); low nibble: case level
// (layout from OpenPods / MagicPodsCore / AirPodsDesktop / librepods, verified on AirPods Max 200A).
//
// The shared BLE advertisement watcher (ble_adverts.cpp) is started the first time a paired Apple
// audio device is seen and keeps running. Nearby AirPods of other people broadcast the same message with random
// addresses, so only messages whose model matches the PID of a paired device are used, and the
// strongest recent signal wins.
#include "drivers.h"
#include "ble_adverts.h"

#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#include <algorithm>
#include <cwchar>
#include <map>
#include <set>
#include <vector>

namespace {

// ---- received messages -------------------------------------------------------------------------

struct Advert {
    UINT16 model = 0;
    bool single = false;
    bool leftBroadcasts = false;
    int left = -1, right = -1, box = -1;  // percent, -1 unknown (single-battery models use `left`)
    bool leftChg = false, rightChg = false, boxChg = false;
    int rssi = -127;
    ULONGLONG tick = 0;
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<UINT64, Advert> g_adverts;  // by (random, rotating) BLE address

int Level(int nibble) { return nibble <= 10 ? nibble * 10 : -1; }

// Single-battery headphones and neckbands (AirPods Max, Beats Solo/Studio/X/Flex/Powerbeats) put
// their battery in the low nibble of byte 6 and leave the upper nibble and case nibble as 0 (not
// 15), so decoding them as dual-bud would always report the unused side as 0%.
bool IsSingleBatteryModel(UINT16 model) {
    switch (model) {
        case 0x200A:  // AirPods Max (Lightning)
        case 0x201F:  // AirPods Max (USB-C)
        case 0x202D:  // AirPods Max 2
        case 0x2003:  // Powerbeats 3
        case 0x2005:  // BeatsX
        case 0x2006:  // Beats Solo 3
        case 0x2009:  // Beats Studio 3
        case 0x200C:  // Beats Solo Pro
        case 0x200D:  // Powerbeats 4
        case 0x2010:  // Beats Flex
        case 0x2017:  // Beats Studio Pro
        case 0x2025:  // Beats Solo 4
            return true;
        default:
            return false;
    }
}

bool ParseProximityTlv(const BYTE* d, UINT32 n, Advert& a) {
    // d[1] == 0x19 (25 bytes following) and d[2] == 0x01 (paired status mode; 0x00 is pairing mode).
    if (n < 27 || d[0] != 0x07 || d[1] != 0x19 || d[2] != 0x01) return false;
    a.model = (UINT16)(d[3] | d[4] << 8);
    if ((a.model & 0xFF00) != 0x2000) return false;
    a.single = IsSingleBatteryModel(a.model);
    a.leftBroadcasts = (d[5] & 0x20) != 0;
    int cur = d[6] & 0x0F, other = d[6] >> 4;
    int chg = d[7] >> 4;
    if (a.single) {
        int pct = Level(cur);
        bool charging = (chg & 0x01) != 0;
        if (cur == 0 && other >= 1 && other <= 10) {
            pct = Level(other);
            charging = (chg & 0x02) != 0;
        }
        if (pct < 0) return false;
        a.left = pct;
        a.leftChg = charging;
        a.right = -1;
        a.box = -1;
        return true;
    }
    a.left = Level(a.leftBroadcasts ? cur : other);
    a.right = Level(a.leftBroadcasts ? other : cur);
    a.leftChg = a.left >= 0 && (chg & (a.leftBroadcasts ? 0x01 : 0x02)) != 0;
    a.rightChg = a.right >= 0 && (chg & (a.leftBroadcasts ? 0x02 : 0x01)) != 0;
    a.box = Level(d[7] & 0x0F);
    a.boxChg = a.box >= 0 && (chg & 0x04) != 0;
    return a.left >= 0 || a.right >= 0 || a.box >= 0;
}

bool Parse(const BYTE* buf, UINT32 n, Advert& a) {
    // Walk the Apple Continuity TLV sequence in case another TLV precedes 0x07.
    for (UINT32 i = 0; i + 1 < n;) {
        BYTE type = buf[i], len = buf[i + 1];
        if (i + 2 + (UINT32)len > n) break;
        if (type == 0x07 && ParseProximityTlv(buf + i, 2 + (UINT32)len, a)) return true;
        i += 2 + (UINT32)len;
    }
    return false;
}

// ---- paired Apple audio devices ----------------------------------------------------------------

const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };

struct AppleDevice {
    std::wstring name;
    UINT16 pid = 0;          // Apple model id, from the "_VID&0001004C_PID&xxxx" service nodes
    bool connected = false;
    bool apple = false;
};

std::wstring Upper(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}

std::vector<AppleDevice> PairedAppleAudio() {
    std::map<std::wstring, AppleDevice> byContainer;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return {};
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512];
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        GUID container{};
        DEVPROPTYPE t;
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &t, (PBYTE)&container, sizeof(container), nullptr, 0)) continue;
        wchar_t g[64];
        StringFromGUID2(container, g, 64);
        AppleDevice& d = byContainer[g];
        std::wstring id = Upper(inst);
        size_t v = id.find(L"_VID&0001004C");
        if (v != std::wstring::npos) {
            d.apple = true;
            size_t p = id.find(L"_PID&", v);
            if (p != std::wstring::npos) d.pid = (UINT16)wcstoul(id.c_str() + p + 5, nullptr, 16);
        }
        if (!wcsncmp(inst, L"BTHENUM\\DEV_", 12)) {
            wchar_t buf[256] = {};
            if (SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_FriendlyName, &t, (PBYTE)buf, sizeof(buf) - sizeof(wchar_t), nullptr, 0))
                d.name = buf;
            DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
            if (SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) && t == DEVPROP_TYPE_BOOLEAN)
                d.connected = conn != DEVPROP_FALSE;
        }
    }
    SetupDiDestroyDeviceInfoList(di);
    std::vector<AppleDevice> out;
    for (auto& [cid, d] : byContainer) {
        std::wstring n = Upper(d.name);
        bool looksApple = n.find(L"AIRPODS") != std::wstring::npos || n.find(L"BEATS") != std::wstring::npos;
        // Apple audio model ids are 0x20xx (0x200A AirPods Max, 0x2014 AirPods Pro 2, 0x2027 AirPods Pro 3);
        // iPhones, iPads and Macs are not. Paired devices with a known 0x20xx PID are kept even when
        // Classic Bluetooth is disconnected so headsets charging on the desk can still report via BLE.
        bool appleAudio = d.apple && (d.pid & 0xFF00) == 0x2000;
        if (appleAudio || (d.connected && looksApple && !d.pid)) out.push_back(d);
    }
    std::sort(out.begin(), out.end(), [](const AppleDevice& a, const AppleDevice& b) {
        return a.connected > b.connected;
    });
    return out;
}

// Best message for one device: the model must match when the PID is known; the strongest signal
// among the last 90 s wins (or the latest one within 5 min when Classic Bluetooth is connected).
// When one bud is in the ear and the other is in the open case, both buds broadcast separately,
// so any missing side/case reading is filled from the latest recent broadcast of the other bud.
bool Pick(const AppleDevice& dev, Advert& best) {
    ULONGLONG now = GetTickCount64();
    ULONGLONG maxAge = dev.connected ? 5 * 60 * 1000 : 90 * 1000;
    int minRssi = dev.connected ? -90 : -80;
    bool found = false;
    AcquireSRWLockShared(&g_lock);
    for (auto& [addr, a] : g_adverts) {
        if (dev.pid && a.model != dev.pid) continue;
        ULONGLONG age = now - a.tick;
        if (age > maxAge || a.rssi < minRssi) continue;
        bool fresh = age < 90 * 1000, bestFresh = found && now - best.tick < 90 * 1000;
        if (!found || (fresh && !bestFresh) || (fresh && bestFresh && a.rssi > best.rssi) ||
            (!fresh && !bestFresh && a.tick > best.tick)) {
            best = a;
            found = true;
        }
    }
    if (found && !best.single) {
        for (auto& [addr, a] : g_adverts) {
            if (a.model != best.model || a.single) continue;
            if (best.tick > a.tick ? best.tick - a.tick > 15 * 1000 : a.tick - best.tick > 15 * 1000) continue;
            if (best.left < 0 && a.left >= 0) { best.left = a.left; best.leftChg = a.leftChg; }
            if (best.right < 0 && a.right >= 0) { best.right = a.right; best.rightChg = a.rightChg; }
            if (best.box < 0 && a.box >= 0) { best.box = a.box; best.boxChg = a.boxChg; }
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return found;
}

std::wstring Pct(int v) { return v < 0 ? L"?" : std::to_wstring(v) + L"%"; }

}  // namespace

void AirPodsOnAdvert(UINT64 addr, int rssi, const BYTE* data, UINT32 len) {
    Advert a;
    if (!Parse(data, len, a)) return;
    a.rssi = rssi;
    a.tick = GetTickCount64();
    AcquireSRWLockExclusive(&g_lock);
    g_adverts[addr] = a;
    for (auto it = g_adverts.begin(); it != g_adverts.end();)  // addresses rotate: drop old ones
        it = a.tick - it->second.tick > 10 * 60 * 1000 ? g_adverts.erase(it) : std::next(it);
    ReleaseSRWLockExclusive(&g_lock);
}

void PollAirPods(PollContext& ctx) {
    auto devices = PairedAppleAudio();
    if (devices.empty()) return;
    if (EnsureBleWatcher()) {
        // Just started: give the headphones a moment to broadcast (about once every 1-2 s).
        for (int i = 0; i < 15; i++) {
            Advert a;
            bool any = false;
            for (auto& d : devices) any |= Pick(d, a);
            if (any) break;
            Sleep(200);
        }
    }
    std::set<UINT16> usedPid;
    for (auto& d : devices) {
        if (d.pid && !d.connected && usedPid.count(d.pid)) continue;
        Advert a;
        if (!Pick(d, a)) continue;
        if (d.pid) usedPid.insert(d.pid);
        DeviceStatus s;
        s.name = d.name.empty() ? L"AirPods" : d.name;
        s.key = L"airpods:" + Hex4(d.pid) + L":" + s.name;
        s.kind = DeviceKind::Headset;
        if (a.single) {
            s.percent = a.left;
            s.charging = a.leftChg;
            s.via = L"Bluetooth";
        } else {
            // Prefer the lower discharging bud (the one in use); only show charging when every online bud is charging.
            int disLo = -1, chgLo = -1;
            if (a.left >= 0) {
                int& dst = a.leftChg ? chgLo : disLo;
                if (dst < 0 || a.left < dst) dst = a.left;
            }
            if (a.right >= 0) {
                int& dst = a.rightChg ? chgLo : disLo;
                if (dst < 0 || a.right < dst) dst = a.right;
            }
            if (disLo >= 0) {
                s.percent = disLo;
                s.charging = false;
            } else if (chgLo >= 0) {
                s.percent = chgLo;
                s.charging = true;
            } else {
                s.percent = a.box;
                s.charging = a.boxChg;
            }
            s.via = L"Bluetooth \x00B7 L " + Pct(a.left) + (a.leftChg ? L"\x26A1" : L"") + L"  R " + Pct(a.right) +
                    (a.rightChg ? L"\x26A1" : L"") + L"  case " + Pct(a.box) + (a.boxChg ? L"\x26A1" : L"");
        }
        s.graceSec = d.connected ? 300 : 0;
        ctx.out.push_back(s);
    }
}
