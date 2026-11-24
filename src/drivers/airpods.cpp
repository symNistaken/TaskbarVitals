// Apple AirPods / Beats (AirPods Max, AirPods Pro 1/2/3 and AirPods 1-5) over Bluetooth.
//
// Two transports are supported:
//   1. Apple Accessory Protocol (AAP) over Bluetooth Classic L2CAP PSM 0x1001 via an installed
//      Windows kernel bridge driver (NTPodsAAP, PodPilotAap, or MagicAAP) when the device is
//      connected over Classic Bluetooth. This provides true 1% battery levels (see airpods_aap.cpp).
//   2. BLE "proximity pairing" advertisements (company 0x004C, type 0x07, length 0x19) via the
//      shared watcher (ble_adverts.cpp), which report 10%-step levels even while charging in a
//      case or before Classic Bluetooth connects.
#include "drivers.h"
#include "ble_adverts.h"
#include "airpods_aap.h"
#include "airpods_protocol.h"

#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#include <algorithm>
#include <cwchar>
#include <map>
#include <set>
#include <vector>

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<UINT64, AirPodsBle::Advert> g_adverts;  // by (random, rotating) BLE address

const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };

struct AppleDevice {
    std::wstring name;
    UINT16 pid = 0;          // Apple model id, from the "_VID&0001004C_PID&xxxx" service nodes
    UINT64 addr = 0;         // 48-bit Bluetooth Classic address from "BTHENUM\DEV_<12 hex>\"
    bool connected = false;
    bool apple = false;
};

std::wstring Upper(std::wstring s) {
    for (auto& c : s) c = towupper(c);
    return s;
}

UINT64 ParseHex12(const wchar_t* p) {
    UINT64 addr = 0;
    for (int i = 0; i < 12; ++i) {
        wchar_t ch = p[i];
        UINT64 nib = 0;
        if (ch >= L'0' && ch <= L'9') nib = static_cast<UINT64>(ch - L'0');
        else if (ch >= L'A' && ch <= L'F') nib = static_cast<UINT64>(ch - L'A' + 10);
        else if (ch >= L'a' && ch <= L'f') nib = static_cast<UINT64>(ch - L'a' + 10);
        else return 0;
        addr = (addr << 4) | nib;
    }
    return addr;
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
        if (_wcsnicmp(inst, L"BTHENUM\\DEV_", 12) == 0) {
            if (wcslen(inst + 12) >= 12) {
                UINT64 parsedAddr = ParseHex12(inst + 12);
                if (parsedAddr) d.addr = parsedAddr;
            }
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

bool PickBle(const AppleDevice& dev, AirPodsBle::Advert& best) {
    std::vector<AirPodsBle::Advert> snapshot;
    AcquireSRWLockShared(&g_lock);
    snapshot.reserve(g_adverts.size());
    for (const auto& [addr, a] : g_adverts) snapshot.push_back(a);
    ReleaseSRWLockShared(&g_lock);
    return AirPodsBle::PickBestAdvert(snapshot, dev.pid, dev.connected, GetTickCount64(), best);
}

std::wstring Pct(int v) { return v < 0 ? L"?" : std::to_wstring(v) + L"%"; }

// Populates dual-bud main level/charging (preferring the lower discharging bud) and detail breakdown.
bool FillDualBudStatus(DeviceStatus& s, int left, bool leftChg, int right, bool rightChg, int box, bool boxChg,
                       const wchar_t* suffix) {
    int disLo = -1, chgLo = -1;
    if (left >= 0) {
        int& dst = leftChg ? chgLo : disLo;
        if (dst < 0 || left < dst) dst = left;
    }
    if (right >= 0) {
        int& dst = rightChg ? chgLo : disLo;
        if (dst < 0 || right < dst) dst = right;
    }
    if (disLo >= 0) {
        s.percent = disLo;
        s.charging = false;
    } else if (chgLo >= 0) {
        s.percent = chgLo;
        s.charging = true;
    } else {
        s.percent = box;
        s.charging = boxChg;
    }
    if (s.percent < 0) return false;
    s.full = s.charging && s.percent >= 100;
    s.detail = L"L " + Pct(left) + (leftChg ? L" \x26A1" : L"") +
               L", R " + Pct(right) + (rightChg ? L" \x26A1" : L"") +
               L", case " + Pct(box) + (boxChg ? L" \x26A1" : L"");
    if (suffix && *suffix) s.detail += suffix;
    // Each part on its own, for the dashboard (PR #3946).
    if (left >= 0) s.batteries.push_back({ L"Left", left, leftChg, left >= 100 && leftChg });
    if (right >= 0) s.batteries.push_back({ L"Right", right, rightChg, right >= 100 && rightChg });
    if (box >= 0) s.batteries.push_back({ L"Case", box, boxChg, box >= 100 && boxChg });
    return true;
}

std::wstring FormatMac12(UINT64 addr) {
    wchar_t buf[20];
    swprintf(buf, 20, L"%012llX", static_cast<unsigned long long>(addr & 0xFFFFFFFFFFFFULL));
    return buf;
}

}  // namespace

void AirPodsOnAdvert(UINT64 addr, int rssi, const BYTE* data, UINT32 len) {
    AirPodsBle::Advert a;
    if (!AirPodsBle::Parse(data, len, a)) return;
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

    bool anyNewAap = false;
    for (const auto& d : devices) {
        if (!d.addr) continue;
        if (d.connected) {
            if (airpods_aap::EnsureSession(d.addr)) anyNewAap = true;
        } else {
            airpods_aap::ReleaseSession(d.addr);
        }
    }

    if (anyNewAap) {
        for (int i = 0; i < 25; ++i) {
            bool allSettled = true;
            for (const auto& d : devices) {
                if (!d.connected || !d.addr) continue;
                airpods_aap::Snapshot snap;
                if (!airpods_aap::GetReading(d.addr, snap) && airpods_aap::IsStarting(d.addr)) {
                    allSettled = false;
                    break;
                }
            }
            if (allSettled) break;
            Sleep(100);
        }
    }

    bool needBle = false;
    for (const auto& d : devices) {
        airpods_aap::Snapshot snap;
        if (!(d.connected && d.addr && airpods_aap::GetReading(d.addr, snap) && snap.battery.HasAny())) {
            needBle = true;
            break;
        }
    }

    if (needBle && EnsureBleWatcher()) {
        // Just started: give the headphones a moment to broadcast (about once every 1-2 s).
        for (int i = 0; i < 15; i++) {
            AirPodsBle::Advert a;
            bool any = false;
            for (auto& d : devices) any |= PickBle(d, a);
            if (any) break;
            Sleep(200);
        }
    }

    std::set<UINT16> usedPid;
    for (auto& d : devices) {
        if (d.pid && !d.connected && usedPid.count(d.pid)) continue;

        DeviceStatus s;
        s.name = d.name.empty() ? L"AirPods" : d.name;
        s.key = L"airpods:" + Hex4(d.pid) + L":" + s.name;
        s.kind = DeviceKind::Headset;
        s.vid = 0x004C;
        s.via = L"Bluetooth";
        s.graceSec = d.connected ? 300 : 0;

        airpods_aap::Snapshot aapSnap;
        if (d.connected && d.addr && airpods_aap::GetReading(d.addr, aapSnap) && aapSnap.battery.HasAny()) {
            const auto& b = aapSnap.battery;
            const bool single = b.singleModel || AirPodsBle::IsSingleBatteryModel(d.pid);
            if (single) {
                int pct = b.single;
                bool chg = b.singleCharging;
                if (pct < 0 && b.left >= 0) { pct = b.left; chg = b.leftCharging; }
                if (pct < 0 && b.right >= 0) { pct = b.right; chg = b.rightCharging; }
                if (pct >= 0) {
                    s.percent = pct;
                    s.charging = chg;
                    s.full = s.charging && s.percent >= 100;
                    s.detail = L"AAP";
                    if (d.pid) usedPid.insert(d.pid);
                    ctx.out.push_back(s);
                    continue;
                }
            } else if (FillDualBudStatus(s, b.left, b.leftCharging, b.right, b.rightCharging,
                                         b.box, b.boxCharging, L" \x00B7 AAP")) {
                if (d.pid) usedPid.insert(d.pid);
                ctx.out.push_back(s);
                continue;
            }
        }

        AirPodsBle::Advert a;
        if (!PickBle(d, a)) continue;
        if (d.pid) usedPid.insert(d.pid);
        if (a.single) {
            s.percent = a.left;
            s.charging = a.leftChg;
            s.full = s.charging && s.percent >= 100;
        } else {
            if (!FillDualBudStatus(s, a.left, a.leftChg, a.right, a.rightChg, a.box, a.boxChg, nullptr))
                continue;
        }
        ctx.out.push_back(s);
    }
}

void ShutdownAirPods() {
    airpods_aap::Shutdown();
}

std::wstring DumpAirPodsStatus() {
    std::wstring out = L"\nApple AirPods / Beats:\n";
    auto devices = PairedAppleAudio();
    if (devices.empty()) {
        out += L"  paired devices: none\n";
    } else {
        for (const auto& d : devices) {
            out += L"  " + (d.name.empty() ? std::wstring(L"(unnamed)") : d.name) +
                   L"  PID=" + Hex4(d.pid) +
                   L"  addr=" + (d.addr ? FormatMac12(d.addr) : L"none") +
                   L"  connected=" + (d.connected ? L"true" : L"false") + L"\n";
        }
    }
    out += airpods_aap::DumpDiagnostics();

    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_lock);
    out += L"  BLE 0x07 cache entries: " + std::to_wstring(g_adverts.size()) + L"\n";
    for (const auto& [addr, a] : g_adverts) {
        ULONGLONG ageSec = now >= a.tick ? (now - a.tick) / 1000 : 0;
        out += L"    BLE=" + FormatMac12(addr) +
               L" model=" + Hex4(a.model) +
               L" rssi=" + std::to_wstring(a.rssi) +
               L" age=" + std::to_wstring(ageSec) + L"s";
        if (a.single) {
            out += L" single=" + Pct(a.left) + (a.leftChg ? L"+" : L"");
        } else {
            out += L" L=" + Pct(a.left) + (a.leftChg ? L"+" : L"") +
                   L" R=" + Pct(a.right) + (a.rightChg ? L"+" : L"") +
                   L" case=" + Pct(a.box) + (a.boxChg ? L"+" : L"");
        }
        out += L"\n";
    }
    ReleaseSRWLockShared(&g_lock);
    return out;
}
