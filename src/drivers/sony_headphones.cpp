// Sony Bluetooth headphones and earbuds (WH-1000XM3 / XM4 / XM5, WF-1000XM3 / XM4 / XM5, WH-CH720N,
// LinkBuds, ULT Wear, ...). Windows shows no battery level for most of them, so they are asked over
// the RFCOMM service the Sony Headphones Connect app uses, with read-only requests (protocol info and
// battery level; frame format in sony_headphones_protocol.h).
//   service {96cc203e-5068-46ad-b32d-e316f5e069ba}  (most models)
//   service {956c7b26-d49a-4ba8-b03f-b17d393cb6e2}  (newer firmware / models)
// Service UUIDs from Gadgetbridge (SonyHeadphonesCoordinator / SonyHeadphonesIoThread).
// Only paired devices that Windows reports as connected and whose Bluetooth profile nodes carry
// Sony's vendor id (BTHENUM ..._VID&0002054C_...) are tried. If the phone app holds the channel on
// the same headphones, they are skipped.
#include <winsock2.h>
#include <ws2bth.h>

#include "drivers.h"
#include "sony_headphones_protocol.h"

#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>

#include <cwchar>
#include <cwctype>
#include <set>

namespace {

// {96cc203e-5068-46ad-b32d-e316f5e069ba}
const GUID kMdrV1 = { 0x96cc203e, 0x5068, 0x46ad, { 0xb3, 0x2d, 0xe3, 0x16, 0xf5, 0xe0, 0x69, 0xba } };
// {956c7b26-d49a-4ba8-b03f-b17d393cb6e2}
const GUID kMdrV2 = { 0x956c7b26, 0xd49a, 0x4ba8, { 0xb0, 0x3f, 0xb1, 0x7d, 0x39, 0x3c, 0xb6, 0xe2 } };

const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };

struct Headphones { std::wstring name; std::wstring addrText; ULONGLONG addr; };

std::wstring Upper(const wchar_t* s) {
    std::wstring r(s);
    for (auto& ch : r) ch = towupper(ch);
    return r;
}

std::vector<Headphones> FindConnectedSony() {
    std::vector<Headphones> out;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;
    // Profile nodes look like BTHENUM\{uuid}_VID&0002054C_PID&0D58\...&88C9E8390284_C00000000:
    // collect the addresses of every node with Sony's Bluetooth SIG vendor id (0x054C, source 2).
    std::set<std::wstring> sony;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    wchar_t inst[512];
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        std::wstring id = Upper(inst);
        if (id.find(L"_VID&0002054C_") == std::wstring::npos) continue;
        size_t tail = id.rfind(L"_C00000000");
        if (tail == std::wstring::npos || tail < 12) continue;
        sony.insert(id.substr(tail - 12, 12));
    }
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        // The device node itself: BTHENUM\DEV_<12 hex digits address>\...
        if (_wcsnicmp(inst, L"BTHENUM\\DEV_", 12) != 0) continue;
        std::wstring hex = Upper(std::wstring(inst + 12, wcsnlen(inst + 12, 12)).c_str());
        if (hex.size() != 12 || !sony.count(hex)) continue;
        wchar_t* end = nullptr;
        ULONGLONG addr = wcstoull(hex.c_str(), &end, 16);
        if (!addr || *end) continue;

        DEVPROPTYPE t;
        DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
        if (!SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) ||
            t != DEVPROP_TYPE_BOOLEAN || conn == DEVPROP_FALSE)
            continue;  // never page a device that is not connected (it would only time out)
        wchar_t name[256] = {};
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_FriendlyName, &t, (PBYTE)name, sizeof(name) - sizeof(wchar_t),
                                       nullptr, 0) ||
            t != DEVPROP_TYPE_STRING)
            continue;
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

// Waits until `s` is readable (read) or connected (write). Returns false on timeout or error.
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

bool SendAll(SOCKET s, const std::vector<std::uint8_t>& data) {
    size_t off = 0;
    while (off < data.size()) {
        if (!WaitSocket(s, true, 1000)) return false;
        int n = send(s, (const char*)data.data() + off, (int)(data.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

class Session {
public:
    explicit Session(SOCKET s) : s_(s), deadline_(GetTickCount64() + 4000) {}

    // Sends a command and waits for the reply whose first byte is `replyId` (or the time limit).
    bool Ask(const std::vector<std::uint8_t>& payload, std::uint8_t replyId, int timeoutMs, SonyMdr::Message& reply) {
        if (!SendAll(s_, SonyMdr::Encode(SonyMdr::kTypeCommand, seq_, payload))) return false;
        ULONGLONG until = GetTickCount64() + (ULONGLONG)timeoutMs;
        if (until > deadline_) until = deadline_;
        SonyMdr::Message m;
        while (Next(m, until)) {
            if (m.type == SonyMdr::kTypeAck) {
                seq_ = m.seq;  // the next command uses the sequence number the headphones expect
                continue;
            }
            if (m.type != SonyMdr::kTypeCommand) continue;
            SendAll(s_, SonyMdr::Encode(SonyMdr::kTypeAck, (std::uint8_t)(1 - m.seq), {}));
            if (!m.payload.empty() && m.payload[0] == replyId) {
                reply = m;
                return true;
            }
        }
        return false;
    }

private:
    bool Next(SonyMdr::Message& m, ULONGLONG until) {
        for (;;) {
            if (SonyMdr::TakeMessage(buf_, m)) return true;
            ULONGLONG now = GetTickCount64();
            if (now >= until || buf_.size() > 8192 || !WaitSocket(s_, false, (int)(until - now))) return false;
            char chunk[512];
            int n = recv(s_, chunk, sizeof(chunk), 0);
            if (n <= 0) return false;
            buf_.insert(buf_.end(), chunk, chunk + n);
        }
    }

    SOCKET s_;
    ULONGLONG deadline_;
    std::uint8_t seq_ = 0;
    std::vector<std::uint8_t> buf_;
};

bool ReadBattery(ULONGLONG addr, const GUID& service, SonyMdr::Battery& b) {
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = addr;
    sa.serviceClassId = service;  // Windows looks the RFCOMM channel up over SDP
    sa.port = 0;
    bool ok = false;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) == 0 || (WSAGetLastError() == WSAEWOULDBLOCK && WaitSocket(s, true, 4000))) {
        Session ses(s);
        SonyMdr::Message m;
        ses.Ask({ 0x00, 0x00 }, 0x01, 1200, m);  // protocol info; the answer itself is not needed
        if (ses.Ask({ 0x10, SonyMdr::kBatterySingle }, 0x11, 1200, m) && SonyMdr::ParseBattery(m.payload, b) &&
            !b.dual) {
            ok = true;
        } else if (ses.Ask({ 0x10, SonyMdr::kBatteryDual }, 0x11, 1200, m) && SonyMdr::ParseBattery(m.payload, b)) {
            ok = b.level >= 0;
            if (ses.Ask({ 0x10, SonyMdr::kBatteryCase }, 0x11, 800, m)) SonyMdr::ParseBattery(m.payload, b);
        }
    }
    closesocket(s);
    return ok;
}

std::wstring Pct(int v) { return v >= 0 ? std::to_wstring(v) + L"%" : L"-"; }

}  // namespace

void PollSonyHeadphones(PollContext& ctx) {
    std::vector<Headphones> list = FindConnectedSony();
    if (list.empty() || !StartWinsock()) return;
    for (const Headphones& h : list) {
        SonyMdr::Battery b;
        if (!ReadBattery(h.addr, kMdrV1, b) && !ReadBattery(h.addr, kMdrV2, b)) continue;
        DeviceStatus s;
        s.key = L"sonymdr:" + h.addrText;
        s.name = h.name;  // the same name the Bluetooth fallback uses, so it skips these headphones
        s.via = L"Bluetooth";
        s.kind = DeviceKind::Headset;
        s.percent = b.level;
        s.charging = b.charging;
        if (b.dual) {
            s.detail = L"L " + Pct(b.left) + L", R " + Pct(b.right);
            if (b.caseLevel >= 0) s.detail += L", case " + Pct(b.caseLevel);
        }
        s.graceSec = 180;  // a busy link (Headphones Connect open on a phone) should not make them flicker
        ctx.out.push_back(s);
    }
}
