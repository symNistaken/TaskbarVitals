// Anker Soundcore Bluetooth headphones and earbuds (Life Q20/Q30/Q35, Space Q45/One/One Pro/A40,
// Liberty 2/3/4/4 NC/4 Pro/5, Life P3/A3i/Dot/Note, P20i/P30i/P40i/R50i, Sport X10/X20, AeroFit...).
//
// Reads battery level and charging status directly over the Soundcore RFCOMM control channel
// (vendor service 0cf12d31-fac3-4553-bd80-d6832e7XXXXX, falling back to standard SPP 00001101).
// This works even when Windows' Hands-Free AG (HFP) service is disabled for audio quality, and gives
// charging status plus left/right levels on TWS earbuds.
//
// Only paired devices that Windows reports as connected and that either expose a Soundcore vendor
// RFCOMM service or carry a Soundcore/Anker name are queried. If another app (e.g. OpenSCQ30) holds
// the RFCOMM channel, the device is skipped and Windows' Bluetooth fallback / grace period handles it.
#include <winsock2.h>
#include <ws2bth.h>

#include "drivers.h"
#include "soundcore_protocol.h"

#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#include <cwchar>
#include <cwctype>
#include <map>

namespace {

// Standard Serial Port Profile: {00001101-0000-1000-8000-00805f9b34fb}
const GUID kSppStandard = { 0x00001101, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
// Google FastPair Message Stream RFCOMM UUID: {df21fe2c-2515-4fdb-8886-f12c4d67927c}
const GUID kFastPair = { 0xDF21FE2C, 0x2515, 0x4FDB, { 0x88, 0x86, 0xF1, 0x2C, 0x4D, 0x67, 0x92, 0x7C } };

const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };
const DEVPROPKEY kBtDeviceAddr = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 1 };
const DEVPROPKEY kBtServiceGuid = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 2 };

// Soundcore vendor RFCOMM UUID prefix: 0cf12d31-fac3-4553-bd80-d6832e7XXXXX
bool IsSoundcoreVendorUuid(const GUID& g) {
    return g.Data1 == 0x0CF12D31 && g.Data2 == 0xFAC3 && g.Data3 == 0x4553 &&
           g.Data4[0] == 0xBD && g.Data4[1] == 0x80 && g.Data4[2] == 0xD6 &&
           g.Data4[3] == 0x83 && g.Data4[4] == 0x2E && (g.Data4[5] & 0xF0) == 0x70;
}

std::string LowerAscii(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t ch : s) out.push_back(ch < 128 ? (char)towlower(ch) : '?');
    return out;
}

bool LooksLikeSoundcore(const std::wstring& name) {
    std::string n = LowerAscii(name);
    static const char* const kHints[] = {
        "soundcore", "anker",
        "life q20", "life q30", "life q35", "life tune", "life p3", "life a3", "life dot", "life note",
        "space q45", "space one", "space a40",
        "liberty ", "sport x10", "sport x20", "aerofit", "aeroclip", "vr p10", "sleep a10", "sleep a20",
    };
    for (const char* h : kHints)
        if (n.find(h) != std::string::npos) return true;
    return false;
}

struct SoundcoreDev {
    std::wstring name;
    std::wstring addrText;
    ULONGLONG addr = 0;
    std::vector<GUID> services;  // vendor UUIDs first, then standard SPP
};

std::vector<SoundcoreDev> FindConnectedSoundcore() {
    std::vector<SoundcoreDev> out;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;

    // First pass: collect Soundcore vendor RFCOMM service GUIDs keyed by Bluetooth address.
    std::map<ULONGLONG, std::vector<GUID>> vendorServices;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        DEVPROPTYPE t;
        GUID svc{};
        if (!SetupDiGetDevicePropertyW(di, &dd, &kBtServiceGuid, &t, (PBYTE)&svc, sizeof(svc), nullptr, 0) ||
            t != DEVPROP_TYPE_GUID || !IsSoundcoreVendorUuid(svc))
            continue;
        wchar_t addrBuf[32] = {};
        if (!SetupDiGetDevicePropertyW(di, &dd, &kBtDeviceAddr, &t, (PBYTE)addrBuf, sizeof(addrBuf) - sizeof(wchar_t), nullptr, 0) ||
            t != DEVPROP_TYPE_STRING)
            continue;
        wchar_t* end = nullptr;
        ULONGLONG addr = wcstoull(addrBuf, &end, 16);
        if (addr && end && !*end) vendorServices[addr].push_back(svc);
    }

    // Second pass: find connected BTHENUM\DEV_<addr> nodes that look like Soundcore devices.
    dd.cbSize = sizeof(dd);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512];
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        if (_wcsnicmp(inst, L"BTHENUM\\DEV_", 12) != 0) continue;
        std::wstring hex(inst + 12, wcsnlen(inst + 12, 12));
        if (hex.size() != 12) continue;
        wchar_t* end = nullptr;
        ULONGLONG addr = wcstoull(hex.c_str(), &end, 16);
        if (!addr || *end) continue;

        DEVPROPTYPE t;
        DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
        if (!SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) ||
            t != DEVPROP_TYPE_BOOLEAN || conn == DEVPROP_FALSE)
            continue;  // never page a device that is not connected

        wchar_t name[256] = {};
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_FriendlyName, &t, (PBYTE)name, sizeof(name) - sizeof(wchar_t),
                                       nullptr, 0) ||
            t != DEVPROP_TYPE_STRING)
            continue;

        auto vs = vendorServices.find(addr);
        bool hasVendor = vs != vendorServices.end() && !vs->second.empty();
        if (!hasVendor && !LooksLikeSoundcore(name)) continue;

        SoundcoreDev dev;
        dev.name = name;
        dev.addrText = hex;
        dev.addr = addr;
        if (hasVendor) dev.services = vs->second;
        dev.services.push_back(kFastPair);
        dev.services.push_back(kSppStandard);
        out.push_back(std::move(dev));
    }

    SetupDiDestroyDeviceInfoList(di);
    return out;
}

bool StartWinsock() {
    static bool ok = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return ok;
}

bool WaitSocket(SOCKET s, bool forWrite, int timeoutMs) {
    fd_set set, err;
    FD_ZERO(&set);
    FD_ZERO(&err);
    FD_SET(s, &set);
    FD_SET(s, &err);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    int r = select(0, forWrite ? nullptr : &set, forWrite ? &set : nullptr, &err, &tv);
    return r > 0 && FD_ISSET(s, &set) && !FD_ISSET(s, &err);
}

// Sends a 10-byte empty Soundcore command and waits for the matching inbound packet.
bool Exchange(SOCKET s, Soundcore::Command cmd, Soundcore::Packet& reply, int timeoutMs) {
    auto req = Soundcore::BuildEmptyRequest(cmd);
    if (send(s, reinterpret_cast<const char*>(req.data()), (int)req.size(), 0) != (int)req.size()) return false;
    std::vector<std::uint8_t> buf;
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeoutMs;
    while (buf.size() < 4096) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline || !WaitSocket(s, false, (int)(deadline - now))) break;
        char chunk[512];
        int n = recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buf.insert(buf.end(), chunk, chunk + n);
        if (Soundcore::FindInboundPacket(buf.data(), buf.size(), cmd, reply)) return true;
    }
    return false;
}

struct CachedModel {
    Soundcore::Scale scale;
    bool dualHint = false;
    bool probed = false;
    int lastPercent = -1;
    bool lastCharging = false;
    bool lastDual = false;
    int lastLeft = -1;
    int lastRight = -1;
};
std::map<ULONGLONG, CachedModel> g_modelCache;  // only touched by the single poll worker thread

bool QueryDevice(const SoundcoreDev& dev, const GUID& service, Soundcore::BatteryStatus& st) {
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = dev.addr;
    sa.serviceClassId = service;  // Windows looks the RFCOMM channel up over SDP
    sa.port = 0;

    bool ok = false;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) == 0 || (WSAGetLastError() == WSAEWOULDBLOCK && WaitSocket(s, true, 2500))) {
        if (InlineIsEqualGUID(service, kFastPair)) {
            std::vector<std::uint8_t> buf;
            ULONGLONG deadline = GetTickCount64() + 3000;
            while (GetTickCount64() < deadline) {
                ULONGLONG now = GetTickCount64();
                if (now >= deadline) break;
                int waitMs = (int)(deadline - now > 300 ? 300 : deadline - now);
                if (!WaitSocket(s, false, waitMs)) continue;
                char chunk[256];
                int n = recv(s, chunk, sizeof(chunk), 0);
                if (n <= 0) break;
                buf.insert(buf.end(), chunk, chunk + n);
                if (Soundcore::ParseFastPairStream(buf.data(), buf.size(), st)) {
                    ok = true;
                    break;
                }
            }
        } else {
            CachedModel& cache = g_modelCache[dev.addr];
            if (!cache.probed) {
                cache.probed = true;
                cache.scale = Soundcore::ScaleFromNameLower(LowerAscii(dev.name));
                Soundcore::Packet sn;
                if (Exchange(s, Soundcore::kCmdSerialAndFw, sn, 600)) {
                    std::string code = Soundcore::ExtractModelCode(sn.body);
                    if (!code.empty()) {
                        cache.scale = Soundcore::ScaleForModelCode(code);
                        cache.dualHint = Soundcore::IsDualModelCode(code);
                    }
                }
            }

            Soundcore::Packet lvl;
            if (Exchange(s, Soundcore::kCmdBatteryLevel, lvl, 1000) &&
                Soundcore::ParseBatteryLevelBody(lvl.body, cache.scale, st)) {
                ok = true;
                Soundcore::Packet chg;
                if (Exchange(s, Soundcore::kCmdBatteryCharging, chg, 800))
                    Soundcore::ApplyBatteryChargingBody(chg.body, st);
            } else {
                // Fallback for models that only report battery inside the 01 01 state update packet.
                Soundcore::Packet state;
                if (Exchange(s, Soundcore::kCmdState, state, 1200)) {
                    std::string code = Soundcore::ExtractModelCode(state.body);
                    if (!code.empty()) {
                        cache.scale = Soundcore::ScaleForModelCode(code);
                        cache.dualHint = Soundcore::IsDualModelCode(code);
                    }
                    ok = Soundcore::ParseStateUpdateBody(state.body, cache.scale, cache.dualHint, st);
                }
            }
        }
    }
    closesocket(s);
    return ok;
}

std::wstring Pct(int v) { return v >= 0 ? std::to_wstring(v) + L"%" : L"-"; }

}  // namespace

void PollSoundcore(PollContext& ctx) {
    std::vector<SoundcoreDev> list = FindConnectedSoundcore();
    if (list.empty() || !StartWinsock()) return;
    for (const SoundcoreDev& d : list) {
        CachedModel& cache = g_modelCache[d.addr];
        Soundcore::BatteryStatus st;
        bool ok = false;
        for (const GUID& svc : d.services) {
            if (QueryDevice(d, svc, st)) { ok = true; break; }
        }
        if (ok && st.percent >= 0) {
            cache.lastPercent = st.percent;
            cache.lastCharging = st.charging;
            cache.lastDual = st.dual;
            cache.lastLeft = st.left;
            cache.lastRight = st.right;
        } else if (cache.lastPercent >= 0) {
            // Retain last known battery level if query timed out momentarily while connected
            st.percent = cache.lastPercent;
            st.charging = cache.lastCharging;
            st.dual = cache.lastDual;
            st.left = cache.lastLeft;
            st.right = cache.lastRight;
            ok = true;
        }

        if (!ok || st.percent < 0) continue;
        DeviceStatus s;
        s.key = L"soundcore:" + d.addrText;
        s.name = d.name;  // matches PollBluetooth's container name so it will not be duplicated
        s.via = L"Bluetooth";
        s.kind = DeviceKind::Headset;
        s.percent = st.percent;
        s.charging = st.charging;
        if (st.dual) s.detail = L"L " + Pct(st.left) + L", R " + Pct(st.right);
        s.graceSec = 1800;  // 30 minutes grace period, matching other Bluetooth headsets
        ctx.out.push_back(s);
    }
}
