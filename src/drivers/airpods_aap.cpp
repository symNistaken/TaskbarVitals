#include "airpods_aap.h"

#include <setupapi.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <map>
#include <memory>
#include <vector>

namespace {

// Device interface GUIDs exposed by Windows AAP L2CAP kernel bridge drivers.
// {C0FFEE00-1337-4A5B-9E6F-A1B2C3D4E5F6} - NTPodsAAP (arctumn/ntpods)
const GUID kGuidNtPodsAap = { 0xC0FFEE00, 0x1337, 0x4A5B, { 0x9E, 0x6F, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6 } };
// {1E9FA4AC-DD4C-4D83-A2A0-A5767809CCBA} - PodPilotAap (kylianxdev/Earlo)
const GUID kGuidPodPilotAap = { 0x1E9FA4AC, 0xDD4C, 0x4D83, { 0xA2, 0xA0, 0xA5, 0x76, 0x78, 0x09, 0xCC, 0xBA } };
// {9EEC98BB-3C54-45D4-A843-7900C4635E08} - MagicAAP (MagicPods / aap-head-tracker)
const GUID kGuidMagicAap = { 0x9EEC98BB, 0x3C54, 0x45D4, { 0xA8, 0x43, 0x79, 0x00, 0xC4, 0x63, 0x5E, 0x08 } };

// NTPodsAAP IOCTL contract (windows/drivers/aap/NTPodsAAP.h).
constexpr DWORD kNtPodsConnect = CTL_CODE(0x8000, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD kNtPodsDisconnect = CTL_CODE(0x8000, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD kNtPodsSend = CTL_CODE(0x8000, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD kNtPodsReceive = CTL_CODE(0x8000, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS);
constexpr DWORD kNtPodsGetStatus = CTL_CODE(0x8000, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS);

// PodPilotAap IOCTL contract (driver/PodPilotAap/Public.h).
constexpr DWORD kPodPilotGetInfo = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORD kPodPilotConnect = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS);
constexpr DWORD kPodPilotDisconnect = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS);
constexpr DWORD kPodPilotSend = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_WRITE_ACCESS);
constexpr DWORD kPodPilotReceive = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS);

constexpr ULONGLONG kRetryBackoffMs = 15 * 1000;

enum class BackendKind { NtPods, PodPilot, MagicAap };

struct InterfaceEntry {
    BackendKind kind;
    const wchar_t* name;
    std::wstring path;
    UINT64 pathMac = 0;
};

struct Session {
    UINT64 btAddr = 0;
    HANDLE thread = nullptr;
    HANDLE stopEvt = nullptr;
    bool running = false;
    bool starting = false;
    ULONGLONG lastAttemptTick = 0;
    std::wstring lastStatus;
    airpods_aap::Snapshot snap;
};

struct SessionStartArgs {
    std::shared_ptr<Session> sess;
    HANDLE stopEvt = nullptr;
    std::vector<InterfaceEntry> candidates;
};

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<UINT64, std::shared_ptr<Session>> g_sessions;

std::wstring FormatMac(UINT64 addr) {
    wchar_t buf[24];
    swprintf(buf, 24, L"%012llX", static_cast<unsigned long long>(addr & 0xFFFFFFFFFFFFULL));
    return buf;
}

// Extracts a 12-hex-digit Bluetooth MAC token outside `{...}` GUID blocks.
UINT64 ExtractMacFromPath(const std::wstring& path) {
    bool inBraces = false;
    std::wstring token;
    auto flushToken = [&]() -> UINT64 {
        if (token.size() == 12) {
            UINT64 val = 0;
            bool ok = true;
            for (wchar_t ch : token) {
                if (!iswxdigit(ch)) {
                    ok = false;
                    break;
                }
                UINT64 nib = 0;
                if (ch >= L'0' && ch <= L'9') nib = static_cast<UINT64>(ch - L'0');
                else if (ch >= L'A' && ch <= L'F') nib = static_cast<UINT64>(ch - L'A' + 10);
                else nib = static_cast<UINT64>(ch - L'a' + 10);
                val = (val << 4) | nib;
            }
            if (ok && val != 0) return val;
        }
        return 0;
    };

    for (wchar_t ch : path) {
        if (ch == L'{') {
            if (UINT64 m = flushToken()) return m;
            token.clear();
            inBraces = true;
            continue;
        }
        if (ch == L'}') {
            inBraces = false;
            token.clear();
            continue;
        }
        if (inBraces) continue;
        if (ch == L'#' || ch == L'&' || ch == L'_' || ch == L'\\' || ch == L'/') {
            if (UINT64 m = flushToken()) return m;
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    return flushToken();
}

void EnumerateInterfaceGuid(const GUID& guid, BackendKind kind, const wchar_t* name,
                            std::vector<InterfaceEntry>& out) {
    HDEVINFO di = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return;

    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &guid, i, &ifd); ++i) {
        DWORD req = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &req, nullptr);
        if (req < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<BYTE> buf(req, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (SetupDiGetDeviceInterfaceDetailW(di, &ifd, detail, req, nullptr, nullptr)) {
            InterfaceEntry e;
            e.kind = kind;
            e.name = name;
            e.path = detail->DevicePath;
            e.pathMac = ExtractMacFromPath(e.path);
            out.push_back(std::move(e));
        }
    }
    SetupDiDestroyDeviceInfoList(di);
}

std::vector<InterfaceEntry> EnumerateAllInterfaces() {
    std::vector<InterfaceEntry> out;
    EnumerateInterfaceGuid(kGuidNtPodsAap, BackendKind::NtPods, L"NTPodsAAP", out);
    EnumerateInterfaceGuid(kGuidPodPilotAap, BackendKind::PodPilot, L"PodPilotAap", out);
    EnumerateInterfaceGuid(kGuidMagicAap, BackendKind::MagicAap, L"MagicAAP", out);
    return out;
}

std::vector<InterfaceEntry> FindMatchingInterfaces(UINT64 btAddr) {
    std::vector<InterfaceEntry> all = EnumerateAllInterfaces();
    if (all.empty()) return {};

    size_t countNtPods = 0, countPodPilot = 0, countMagic = 0;
    for (const auto& e : all) {
        if (e.kind == BackendKind::NtPods) ++countNtPods;
        else if (e.kind == BackendKind::PodPilot) ++countPodPilot;
        else ++countMagic;
    }

    std::vector<InterfaceEntry> matched;
    for (const auto& e : all) {
        if (e.pathMac == btAddr) {
            matched.push_back(e);
        } else if (e.pathMac == 0) {
            size_t cnt = e.kind == BackendKind::NtPods ? countNtPods
                         : (e.kind == BackendKind::PodPilot ? countPodPilot : countMagic);
            if (cnt == 1) matched.push_back(e);
        }
    }
    return matched;
}

void RecordBattery(const std::shared_ptr<Session>& sess, const wchar_t* backend,
                   const AirPodsAap::BatteryReading& parsed) {
    AcquireSRWLockExclusive(&g_lock);
    AirPodsAap::MergeBattery(sess->snap.battery, parsed);
    sess->snap.updatedTick = GetTickCount64();
    sess->snap.connected = true;
    sess->snap.backend = backend;
    sess->starting = false;
    sess->lastStatus = L"connected";
    ReleaseSRWLockExclusive(&g_lock);
}

void SetSessionConnected(const std::shared_ptr<Session>& sess, const wchar_t* backend, bool connected,
                         const std::wstring& statusText) {
    AcquireSRWLockExclusive(&g_lock);
    sess->snap.connected = connected;
    if (backend && *backend) sess->snap.backend = backend;
    sess->lastStatus = statusText;
    ReleaseSRWLockExclusive(&g_lock);
}

bool WaitStop(HANDLE stopEvt, DWORD ms) {
    return WaitForSingleObject(stopEvt, ms) == WAIT_OBJECT_0;
}

bool OverlappedIoctlSync(HANDLE h, DWORD code, const void* inBuf, DWORD inLen, void* outBuf, DWORD outLen,
                         DWORD& bytesReturned, HANDLE ioEvt, HANDLE stopEvt, DWORD timeoutMs) {
    ResetEvent(ioEvt);
    OVERLAPPED ov{};
    ov.hEvent = ioEvt;
    bytesReturned = 0;
    BOOL ok = DeviceIoControl(h, code, const_cast<void*>(inBuf), inLen, outBuf, outLen, &bytesReturned, &ov);
    if (ok) {
        return GetOverlappedResult(h, &ov, &bytesReturned, FALSE) != FALSE;
    }
    if (GetLastError() != ERROR_IO_PENDING) return false;
    HANDLE waits[2] = { stopEvt, ioEvt };
    DWORD w = WaitForMultipleObjects(2, waits, FALSE, timeoutMs);
    if (w == WAIT_OBJECT_0 + 1) {
        return GetOverlappedResult(h, &ov, &bytesReturned, FALSE) != FALSE;
    }
    CancelIoEx(h, &ov);
    GetOverlappedResult(h, &ov, &bytesReturned, TRUE);
    return false;
}

bool OverlappedWriteSync(HANDLE h, const void* data, DWORD len, HANDLE ioEvt, HANDLE stopEvt, DWORD timeoutMs) {
    ResetEvent(ioEvt);
    OVERLAPPED ov{};
    ov.hEvent = ioEvt;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data, len, &written, &ov);
    if (ok) {
        return GetOverlappedResult(h, &ov, &written, FALSE) != FALSE && written == len;
    }
    if (GetLastError() != ERROR_IO_PENDING) return false;
    HANDLE waits[2] = { stopEvt, ioEvt };
    DWORD w = WaitForMultipleObjects(2, waits, FALSE, timeoutMs);
    if (w == WAIT_OBJECT_0 + 1) {
        return GetOverlappedResult(h, &ov, &written, FALSE) != FALSE && written == len;
    }
    CancelIoEx(h, &ov);
    GetOverlappedResult(h, &ov, &written, TRUE);
    return false;
}

template <class SendFn>
bool SendHandshakeSequence(SendFn&& sendFn, HANDLE stopEvt) {
    if (!sendFn(AirPodsAap::kHandshake, sizeof(AirPodsAap::kHandshake))) return false;
    if (WaitStop(stopEvt, 150)) return false;
    if (!sendFn(AirPodsAap::kHostCapabilities, sizeof(AirPodsAap::kHostCapabilities))) return false;
    if (WaitStop(stopEvt, 150)) return false;
    if (!sendFn(AirPodsAap::kRequestNotifications, sizeof(AirPodsAap::kRequestNotifications))) return false;
    return true;
}

bool RunNtPodsSession(const std::shared_ptr<Session>& sess, const InterfaceEntry& iface, HANDLE stopEvt) {
    HANDLE h = CreateFileW(iface.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetSessionConnected(sess, iface.name, false, L"open failed (" + std::to_wstring(GetLastError()) + L")");
        return false;
    }

    uint8_t connIn[10] = {};
    for (int i = 0; i < 8; ++i) connIn[i] = static_cast<uint8_t>((sess->btAddr >> (i * 8)) & 0xFF);
    connIn[8] = static_cast<uint8_t>(AirPodsAap::kPsm & 0xFF);
    connIn[9] = static_cast<uint8_t>((AirPodsAap::kPsm >> 8) & 0xFF);
    uint8_t connOut[8] = {};
    DWORD br = 0;
    if (!DeviceIoControl(h, kNtPodsConnect, connIn, sizeof(connIn), connOut, sizeof(connOut), &br, nullptr) ||
        br < 8) {
        SetSessionConnected(sess, iface.name, false, L"connect ioctl failed (" + std::to_wstring(GetLastError()) + L")");
        CloseHandle(h);
        return false;
    }
    uint32_t connSuccess = 0;
    std::memcpy(&connSuccess, connOut, sizeof(connSuccess));
    if (connSuccess == 0) {
        int32_t ntStatus = 0;
        std::memcpy(&ntStatus, connOut + 4, sizeof(ntStatus));
        wchar_t msg[64];
        swprintf(msg, 64, L"connect refused (0x%08X)", static_cast<unsigned>(ntStatus));
        SetSessionConnected(sess, iface.name, false, msg);
        CloseHandle(h);
        return false;
    }

    auto disconnectAndClose = [&]() {
        DWORD dbr = 0;
        DeviceIoControl(h, kNtPodsDisconnect, nullptr, 0, nullptr, 0, &dbr, nullptr);
        CloseHandle(h);
    };

    auto sendFn = [&](const uint8_t* data, DWORD len) -> bool {
        DWORD sbr = 0;
        return DeviceIoControl(h, kNtPodsSend, const_cast<uint8_t*>(data), len, nullptr, 0, &sbr, nullptr) != FALSE;
    };

    if (!SendHandshakeSequence(sendFn, stopEvt)) {
        SetSessionConnected(sess, iface.name, false, L"handshake failed");
        disconnectAndClose();
        return WaitStop(stopEvt, 0);
    }

    SetSessionConnected(sess, iface.name, true, L"handshaked");

    uint8_t recvBuf[1024];
    uint32_t timeoutMs = 1000;
    while (!WaitStop(stopEvt, 0)) {
        DWORD n = 0;
        if (DeviceIoControl(h, kNtPodsReceive, &timeoutMs, sizeof(timeoutMs), recvBuf, sizeof(recvBuf), &n, nullptr)) {
            if (n > 0) {
                AirPodsAap::BatteryReading parsed;
                if (AirPodsAap::ParseBatteryPacket(recvBuf, n, parsed)) {
                    RecordBattery(sess, iface.name, parsed);
                }
            }
            continue;
        }
        DWORD err = GetLastError();
        if (err == ERROR_SEM_TIMEOUT || err == ERROR_TIMEOUT || err == WAIT_TIMEOUT) {
            uint8_t st[32] = {};
            DWORD stBytes = 0;
            if (DeviceIoControl(h, kNtPodsGetStatus, nullptr, 0, st, sizeof(st), &stBytes, nullptr) && stBytes >= 4) {
                uint32_t state = 0;
                std::memcpy(&state, st, sizeof(state));
                if (state != 2) break;
                continue;
            }
            break;
        }
        break;
    }

    SetSessionConnected(sess, iface.name, false, L"disconnected");
    disconnectAndClose();
    return true;
}

bool RunPodPilotSession(const std::shared_ptr<Session>& sess, const InterfaceEntry& iface, HANDLE stopEvt) {
    HANDLE h = CreateFileW(iface.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetSessionConnected(sess, iface.name, false, L"open failed (" + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    HANDLE ioEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ioEvt) {
        CloseHandle(h);
        return false;
    }

    uint8_t info[24] = {};
    DWORD br = 0;
    if (OverlappedIoctlSync(h, kPodPilotGetInfo, nullptr, 0, info, sizeof(info), br, ioEvt, stopEvt, 1500) &&
        br >= 16) {
        uint64_t devAddr = 0;
        std::memcpy(&devAddr, info + 8, sizeof(devAddr));
        if (devAddr != 0 && devAddr != sess->btAddr) {
            CloseHandle(ioEvt);
            CloseHandle(h);
            return false;
        }
    }

    if (!OverlappedIoctlSync(h, kPodPilotConnect, nullptr, 0, nullptr, 0, br, ioEvt, stopEvt, 5000)) {
        SetSessionConnected(sess, iface.name, false, L"connect failed (" + std::to_wstring(GetLastError()) + L")");
        CloseHandle(ioEvt);
        CloseHandle(h);
        return false;
    }

    auto disconnectAndClose = [&]() {
        DWORD dbr = 0;
        HANDLE discEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (discEvt) {
            OVERLAPPED dov{};
            dov.hEvent = discEvt;
            if (!DeviceIoControl(h, kPodPilotDisconnect, nullptr, 0, nullptr, 0, &dbr, &dov) &&
                GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(discEvt, 1000) != WAIT_OBJECT_0) CancelIoEx(h, &dov);
                GetOverlappedResult(h, &dov, &dbr, TRUE);
            }
            CloseHandle(discEvt);
        }
        CloseHandle(ioEvt);
        CloseHandle(h);
    };

    auto sendFn = [&](const uint8_t* data, DWORD len) -> bool {
        DWORD sbr = 0;
        return OverlappedIoctlSync(h, kPodPilotSend, data, len, nullptr, 0, sbr, ioEvt, stopEvt, 2000);
    };

    if (!SendHandshakeSequence(sendFn, stopEvt)) {
        SetSessionConnected(sess, iface.name, false, L"handshake failed");
        disconnectAndClose();
        return WaitStop(stopEvt, 0);
    }

    SetSessionConnected(sess, iface.name, true, L"handshaked");

    uint8_t recvBuf[1024];
    bool stopping = false;
    while (!stopping && !WaitStop(stopEvt, 0)) {
        ResetEvent(ioEvt);
        OVERLAPPED ov{};
        ov.hEvent = ioEvt;
        DWORD n = 0;
        BOOL ok = DeviceIoControl(h, kPodPilotReceive, nullptr, 0, recvBuf, sizeof(recvBuf), &n, &ov);
        bool hasData = false;
        if (ok) {
            hasData = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = { stopEvt, ioEvt };
            while (true) {
                DWORD w = WaitForMultipleObjects(2, waits, FALSE, 1000);
                if (w == WAIT_TIMEOUT) continue;
                if (w == WAIT_OBJECT_0 + 1) {
                    hasData = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
                    break;
                }
                CancelIoEx(h, &ov);
                GetOverlappedResult(h, &ov, &n, TRUE);
                stopping = true;
                break;
            }
        } else {
            break;
        }
        if (stopping || !hasData) break;
        if (n > 0) {
            AirPodsAap::BatteryReading parsed;
            if (AirPodsAap::ParseBatteryPacket(recvBuf, n, parsed)) {
                RecordBattery(sess, iface.name, parsed);
            }
        }
    }

    SetSessionConnected(sess, iface.name, false, L"disconnected");
    disconnectAndClose();
    return true;
}

bool RunMagicAapSession(const std::shared_ptr<Session>& sess, const InterfaceEntry& iface, HANDLE stopEvt) {
    HANDLE h = CreateFileW(iface.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        SetSessionConnected(sess, iface.name, false, L"open failed (" + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    HANDLE ioEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ioEvt) {
        CloseHandle(h);
        return false;
    }

    auto sendFn = [&](const uint8_t* data, DWORD len) -> bool {
        return OverlappedWriteSync(h, data, len, ioEvt, stopEvt, 2000);
    };

    if (!SendHandshakeSequence(sendFn, stopEvt)) {
        SetSessionConnected(sess, iface.name, false, L"handshake failed");
        CloseHandle(ioEvt);
        CloseHandle(h);
        return WaitStop(stopEvt, 0);
    }

    SetSessionConnected(sess, iface.name, true, L"handshaked");

    uint8_t recvBuf[2048];
    std::vector<uint8_t> streamBuf;
    bool stopping = false;
    while (!stopping && !WaitStop(stopEvt, 0)) {
        ResetEvent(ioEvt);
        OVERLAPPED ov{};
        ov.hEvent = ioEvt;
        DWORD n = 0;
        BOOL ok = ReadFile(h, recvBuf, sizeof(recvBuf), &n, &ov);
        bool hasData = false;
        if (ok) {
            hasData = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = { stopEvt, ioEvt };
            while (true) {
                DWORD w = WaitForMultipleObjects(2, waits, FALSE, 1000);
                if (w == WAIT_TIMEOUT) continue;
                if (w == WAIT_OBJECT_0 + 1) {
                    hasData = GetOverlappedResult(h, &ov, &n, FALSE) != FALSE;
                    break;
                }
                CancelIoEx(h, &ov);
                GetOverlappedResult(h, &ov, &n, TRUE);
                stopping = true;
                break;
            }
        } else {
            break;
        }
        if (stopping || !hasData) break;
        if (n > 0) {
            streamBuf.insert(streamBuf.end(), recvBuf, recvBuf + n);
            AirPodsAap::BatteryReading delta;
            if (AirPodsAap::ExtractBatteryPackets(streamBuf, delta)) {
                RecordBattery(sess, iface.name, delta);
            }
        }
    }

    SetSessionConnected(sess, iface.name, false, L"disconnected");
    CloseHandle(ioEvt);
    CloseHandle(h);
    return true;
}

DWORD WINAPI SessionThreadProc(LPVOID param) {
    std::unique_ptr<SessionStartArgs> args(static_cast<SessionStartArgs*>(param));
    const std::shared_ptr<Session>& sess = args->sess;
    HANDLE stopEvt = args->stopEvt;

    for (const auto& iface : args->candidates) {
        if (WaitStop(stopEvt, 0)) break;
        bool handled = false;
        if (iface.kind == BackendKind::NtPods) handled = RunNtPodsSession(sess, iface, stopEvt);
        else if (iface.kind == BackendKind::PodPilot) handled = RunPodPilotSession(sess, iface, stopEvt);
        else handled = RunMagicAapSession(sess, iface, stopEvt);
        if (handled) break;
    }

    AcquireSRWLockExclusive(&g_lock);
    sess->running = false;
    sess->starting = false;
    sess->snap.connected = false;
    sess->lastAttemptTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
    return 0;
}

}  // namespace

namespace airpods_aap {

bool EnsureSession(UINT64 btAddr) {
    if (!btAddr) return false;
    const ULONGLONG now = GetTickCount64();

    AcquireSRWLockShared(&g_lock);
    auto it = g_sessions.find(btAddr);
    if (it != g_sessions.end()) {
        const auto& s = it->second;
        if (s->running || (now - s->lastAttemptTick < kRetryBackoffMs)) {
            ReleaseSRWLockShared(&g_lock);
            return false;
        }
    }
    ReleaseSRWLockShared(&g_lock);

    std::vector<InterfaceEntry> candidates = FindMatchingInterfaces(btAddr);
    if (candidates.empty()) return false;

    AcquireSRWLockExclusive(&g_lock);
    auto& slot = g_sessions[btAddr];
    if (!slot) slot = std::make_shared<Session>();
    if (slot->running || (GetTickCount64() - slot->lastAttemptTick < kRetryBackoffMs)) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }

    if (slot->thread) {
        WaitForSingleObject(slot->thread, 1000);
        CloseHandle(slot->thread);
        slot->thread = nullptr;
    }
    if (slot->stopEvt) {
        CloseHandle(slot->stopEvt);
        slot->stopEvt = nullptr;
    }

    slot->btAddr = btAddr;
    slot->stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!slot->stopEvt) {
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    slot->running = true;
    slot->starting = true;
    slot->lastAttemptTick = GetTickCount64();
    slot->snap.connected = false;
    slot->lastStatus = L"starting";

    auto* args = new SessionStartArgs{ slot, slot->stopEvt, std::move(candidates) };
    HANDLE th = CreateThread(nullptr, 0, SessionThreadProc, args, 0, nullptr);
    if (!th) {
        delete args;
        slot->running = false;
        slot->starting = false;
        CloseHandle(slot->stopEvt);
        slot->stopEvt = nullptr;
        ReleaseSRWLockExclusive(&g_lock);
        return false;
    }
    slot->thread = th;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

bool IsStarting(UINT64 btAddr) {
    AcquireSRWLockShared(&g_lock);
    auto it = g_sessions.find(btAddr);
    bool starting = it != g_sessions.end() && it->second->running && it->second->starting;
    ReleaseSRWLockShared(&g_lock);
    return starting;
}

void ReleaseSession(UINT64 btAddr) {
    if (!btAddr) return;
    std::shared_ptr<Session> sess;
    HANDLE th = nullptr;
    HANDLE stopEvt = nullptr;

    AcquireSRWLockExclusive(&g_lock);
    auto it = g_sessions.find(btAddr);
    if (it != g_sessions.end()) {
        sess = it->second;
        th = sess->thread;
        stopEvt = sess->stopEvt;
        sess->thread = nullptr;
        sess->stopEvt = nullptr;
        sess->running = false;
        sess->starting = false;
        sess->snap.connected = false;
        g_sessions.erase(it);
    }
    ReleaseSRWLockExclusive(&g_lock);

    if (stopEvt) SetEvent(stopEvt);
    DWORD waitRes = WAIT_OBJECT_0;
    if (th) {
        waitRes = WaitForSingleObject(th, 3000);
        CloseHandle(th);
    }
    if (stopEvt && waitRes == WAIT_OBJECT_0) CloseHandle(stopEvt);
}

bool GetReading(UINT64 btAddr, Snapshot& out) {
    if (!btAddr) return false;
    AcquireSRWLockShared(&g_lock);
    auto it = g_sessions.find(btAddr);
    bool ok = false;
    if (it != g_sessions.end()) {
        const auto& s = it->second;
        if (s->snap.connected && s->snap.battery.HasAny()) {
            out = s->snap;
            ok = true;
        }
    }
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

void Shutdown() {
    struct PendingStop {
        std::shared_ptr<Session> sess;
        HANDLE th = nullptr;
        HANDLE stopEvt = nullptr;
    };
    std::vector<PendingStop> list;

    AcquireSRWLockExclusive(&g_lock);
    for (auto& [addr, s] : g_sessions) {
        PendingStop p;
        p.sess = s;
        p.th = s->thread;
        p.stopEvt = s->stopEvt;
        s->thread = nullptr;
        s->stopEvt = nullptr;
        s->running = false;
        s->starting = false;
        s->snap.connected = false;
        list.push_back(std::move(p));
    }
    g_sessions.clear();
    ReleaseSRWLockExclusive(&g_lock);

    for (auto& p : list) {
        if (p.stopEvt) SetEvent(p.stopEvt);
    }
    for (auto& p : list) {
        DWORD waitRes = WAIT_OBJECT_0;
        if (p.th) {
            waitRes = WaitForSingleObject(p.th, 3000);
            CloseHandle(p.th);
        }
        if (p.stopEvt && waitRes == WAIT_OBJECT_0) CloseHandle(p.stopEvt);
    }
}

std::wstring DumpDiagnostics() {
    std::wstring out;
    auto ifaces = EnumerateAllInterfaces();
    out += L"AAP bridge interfaces: " + (ifaces.empty() ? std::wstring(L"none found") : std::to_wstring(ifaces.size())) + L"\n";
    for (const auto& e : ifaces) {
        out += L"  [" + std::wstring(e.name) + L"] mac=" + (e.pathMac ? FormatMac(e.pathMac) : L"unknown") +
               L"  path=" + e.path + L"\n";
    }

    AcquireSRWLockShared(&g_lock);
    for (const auto& [addr, s] : g_sessions) {
        out += L"  AAP session " + FormatMac(addr) + L": backend=" +
               (s->snap.backend.empty() ? L"-" : s->snap.backend) + L" connected=" +
               (s->snap.connected ? L"true" : L"false") + L" status=" + s->lastStatus + L"\n";
    }
    ReleaseSRWLockShared(&g_lock);
    return out;
}

}  // namespace airpods_aap
