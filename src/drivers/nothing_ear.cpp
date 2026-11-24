// Nothing / CMF wireless earbuds (Nothing Ear (3a), Ear (a), Ear (2), Ear, CMF Buds, ...).
// Windows does not expose a battery level for them over Classic Bluetooth, so they are queried
// over their NT Link RFCOMM service ({aeac4a03-dff5-498f-843a-34487cf133eb}) using the read-only
// 0xC007 battery command (protocol in nothing_ear_protocol.h).
// Only paired devices that Windows reports as connected and whose name matches Nothing/CMF earbuds
// are tried. If another app holds the RFCOMM channel, the earbuds are skipped.
#include <winsock2.h>
#include <ws2bth.h>

#include "drivers.h"
#include "nothing_ear_protocol.h"

#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#include <cwchar>
#include <cwctype>

namespace {

// {aeac4a03-dff5-498f-843a-34487cf133eb}
const GUID kNtLink = { 0xaeac4a03, 0xdff5, 0x498f, { 0x84, 0x3a, 0x34, 0x48, 0x7c, 0xf1, 0x33, 0xeb } };

const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };

struct Earbuds { std::wstring name; std::wstring addrText; ULONGLONG addr; };

bool LooksLikeNothingEar(const std::wstring& name) {
    std::wstring n = name;
    for (auto& ch : n) ch = towlower(ch);
    if (n.find(L"nothing") != std::wstring::npos &&
        (n.find(L"ear") != std::wstring::npos || n.find(L"buds") != std::wstring::npos))
        return true;
    return n.find(L"ear (") != std::wstring::npos || n.find(L"ear 3a") != std::wstring::npos ||
           n.find(L"cmf buds") != std::wstring::npos;
}

std::vector<Earbuds> FindConnectedEarbuds() {
    std::vector<Earbuds> out;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
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
            continue;
        wchar_t name[256] = {};
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_FriendlyName, &t, (PBYTE)name, sizeof(name) - sizeof(wchar_t),
                                       nullptr, 0) ||
            t != DEVPROP_TYPE_STRING)
            continue;
        if (!LooksLikeNothingEar(name)) continue;
        out.push_back({ name, hex, addr });
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

bool ReadStatus(ULONGLONG addr, NothingEar::Status& st) {
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = addr;
    sa.serviceClassId = kNtLink;
    sa.port = 0;
    bool found = false;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) == 0 || (WSAGetLastError() == WSAEWOULDBLOCK && WaitSocket(s, true, 3000))) {
        auto query = NothingEar::BuildBatteryQuery(1);
        if (send(s, (const char*)query.data(), (int)query.size(), 0) == (int)query.size()) {
            std::vector<std::uint8_t> buf;
            ULONGLONG deadline = GetTickCount64() + 2000;
            while (!found && buf.size() < 4096) {
                ULONGLONG now = GetTickCount64();
                if (now >= deadline || !WaitSocket(s, false, (int)(deadline - now))) break;
                char chunk[512];
                int n = recv(s, chunk, sizeof(chunk), 0);
                if (n <= 0) break;
                buf.insert(buf.end(), chunk, chunk + n);
                found = NothingEar::FindStatus(buf.data(), buf.size(), st);
            }
        }
    }
    closesocket(s);
    return found;
}

std::wstring Pct(int v) { return v >= 0 ? std::to_wstring(v) + L"%" : L"-"; }

}  // namespace

void PollNothingEar(PollContext& ctx) {
    std::vector<Earbuds> list = FindConnectedEarbuds();
    if (list.empty() || !StartWinsock()) return;
    for (const Earbuds& b : list) {
        NothingEar::Status st;
        if (!ReadStatus(b.addr, st)) continue;
        DeviceStatus s;
        s.key = L"nothingear:" + b.addrText;
        s.name = b.name;
        s.via = L"Bluetooth";
        s.kind = DeviceKind::Headset;
        if (st.left >= 0 && st.right >= 0) s.percent = st.left < st.right ? st.left : st.right;
        else s.percent = st.left >= 0 ? st.left : st.right;
        s.charging = st.leftCharging || st.rightCharging;
        s.detail = L"L " + Pct(st.left) + L", R " + Pct(st.right);
        if (st.caseLevel >= 0) s.detail += L", case " + Pct(st.caseLevel);
        if (st.left >= 0) s.batteries.push_back({ L"Left", st.left, st.leftCharging, st.left >= 100 && st.leftCharging });  // PR #3946
        if (st.right >= 0) s.batteries.push_back({ L"Right", st.right, st.rightCharging, st.right >= 100 && st.rightCharging });
        if (st.caseLevel >= 0) s.batteries.push_back({ L"Case", st.caseLevel, st.caseCharging, st.caseLevel >= 100 && st.caseCharging });
        s.graceSec = 180;
        ctx.out.push_back(s);
    }
}
