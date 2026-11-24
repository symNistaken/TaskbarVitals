#include "netmon.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace netmon {
namespace {

bool g_havePrev = false;
std::uint64_t g_prevRx = 0, g_prevTx = 0;
std::uint64_t g_prevTick = 0;

void SumActiveOctets(std::uint64_t& rxTotal, std::uint64_t& txTotal, bool& any) {
    rxTotal = txTotal = 0;
    any = false;
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return;

    bool anyHardware = false;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) continue;
        if (row.OperStatus != IfOperStatusUp) continue;
        if (row.InterfaceAndOperStatusFlags.HardwareInterface) anyHardware = true;
    }
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL) continue;
        if (row.OperStatus != IfOperStatusUp) continue;
        if (anyHardware && !row.InterfaceAndOperStatusFlags.HardwareInterface) continue;
        rxTotal += row.InOctets;
        txTotal += row.OutOctets;
        any = true;
    }
    FreeMibTable(table);
}

// ---- Background one-click speed test (WinHTTP) ----
SRWLOCK g_testLock = SRWLOCK_INIT;
SpeedTestResult g_testResult;
std::atomic<bool> g_cancel{ false };
HANDLE g_testThread = nullptr;

struct WinHttpHandle {
    HINTERNET h = nullptr;
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};

bool RunDownload(HINTERNET conn, double& downMbpsOut) {
    WinHttpHandle req{ WinHttpOpenRequest(conn, L"GET", L"/__down?bytes=10000000", nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
    if (!req) return false;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr))
        return false;
    DWORD status = 0, ssz = sizeof(status);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &ssz, WINHTTP_NO_HEADER_INDEX) && status >= 400)
        return false;

    std::vector<BYTE> buf(65536);
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    std::uint64_t total = 0;
    for (;;) {
        if (g_cancel.load()) return false;
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        while (avail > 0) {
            if (g_cancel.load()) return false;
            DWORD chunk = std::min<DWORD>(avail, (DWORD)buf.size()), got = 0;
            if (!WinHttpReadData(req, buf.data(), chunk, &got) || got == 0) break;
            total += got;
            avail -= got;
        }
    }
    QueryPerformanceCounter(&t1);
    double secs = freq.QuadPart > 0 ? (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart : 0.0;
    if (total == 0 || secs <= 0.01) return false;
    downMbpsOut = (double)total * 8.0 / (secs * 1000000.0);
    return true;
}

bool RunUpload(HINTERNET conn, double& upMbpsOut) {
    constexpr DWORD kUploadBytes = 2 * 1024 * 1024;
    WinHttpHandle req{ WinHttpOpenRequest(conn, L"POST", L"/__up", nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
    if (!req) return false;
    std::vector<BYTE> payload(kUploadBytes, 0x55);
    const wchar_t* hdr = L"Content-Type: application/octet-stream\r\n";
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    if (!WinHttpSendRequest(req, hdr, (DWORD)wcslen(hdr), payload.data(), kUploadBytes, kUploadBytes, 0) ||
        !WinHttpReceiveResponse(req, nullptr))
        return false;
    QueryPerformanceCounter(&t1);
    DWORD status = 0, ssz = sizeof(status);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &ssz, WINHTTP_NO_HEADER_INDEX) && status >= 400)
        return false;
    double secs = freq.QuadPart > 0 ? (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart : 0.0;
    if (secs <= 0.01) return false;
    upMbpsOut = (double)kUploadBytes * 8.0 / (secs * 1000000.0);
    return true;
}

DWORD WINAPI SpeedTestWorker(LPVOID) {
    double downMbps = -1.0, upMbps = -1.0;
    bool ok = false;
    WinHttpHandle session{ WinHttpOpen(L"TaskbarVitals/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (session) {
        WinHttpSetTimeouts(session, 5000, 5000, 12000, 12000);
        WinHttpHandle conn{ WinHttpConnect(session, L"speed.cloudflare.com", INTERNET_DEFAULT_HTTPS_PORT, 0) };
        if (conn && RunDownload(conn, downMbps) && !g_cancel.load()) {
            RunUpload(conn, upMbps);
            ok = true;
        }
    }
    AcquireSRWLockExclusive(&g_testLock);
    g_testResult.downMbps = ok ? downMbps : -1.0;
    g_testResult.upMbps = ok ? upMbps : -1.0;
    g_testResult.state = g_cancel.load() ? TestState::Idle : (ok ? TestState::Done : TestState::Failed);
    g_testResult.error = ok ? L"" : L"Connection failed";
    g_testResult.finishedTick = GetTickCount64();
    ReleaseSRWLockExclusive(&g_testLock);
    return 0;
}

}  // namespace

Reading Sample() {
    Reading r;
    std::uint64_t rx = 0, tx = 0;
    bool any = false;
    SumActiveOctets(rx, tx, any);
    if (!any) {
        g_havePrev = false;
        return r;
    }
    r.available = true;
    std::uint64_t now = GetTickCount64();
    if (g_havePrev && now > g_prevTick) {
        std::uint64_t dt = now - g_prevTick;
        r.downBytesPerSec = RateBytesPerSec(g_prevRx, rx, dt);
        r.upBytesPerSec = RateBytesPerSec(g_prevTx, tx, dt);
    }
    g_prevRx = rx;
    g_prevTx = tx;
    g_prevTick = now;
    g_havePrev = true;
    return r;
}

bool StartSpeedTest() {
    AcquireSRWLockExclusive(&g_testLock);
    if (g_testResult.state == TestState::Running) {
        ReleaseSRWLockExclusive(&g_testLock);
        return false;
    }
    if (g_testThread) {
        CloseHandle(g_testThread);
        g_testThread = nullptr;
    }
    g_cancel.store(false);
    g_testResult = SpeedTestResult{ TestState::Running, -1.0, -1.0, L"", 0 };
    ReleaseSRWLockExclusive(&g_testLock);

    g_testThread = CreateThread(nullptr, 0, SpeedTestWorker, nullptr, 0, nullptr);
    if (!g_testThread) {
        AcquireSRWLockExclusive(&g_testLock);
        g_testResult.state = TestState::Failed;
        ReleaseSRWLockExclusive(&g_testLock);
        return false;
    }
    return true;
}

SpeedTestResult GetSpeedTest() {
    AcquireSRWLockShared(&g_testLock);
    SpeedTestResult copy = g_testResult;
    ReleaseSRWLockShared(&g_testLock);
    return copy;
}

void Stop() {
    g_cancel.store(true);
    if (g_testThread) {
        WaitForSingleObject(g_testThread, 1500);
        CloseHandle(g_testThread);
        g_testThread = nullptr;
    }
    g_havePrev = false;
}

}  // namespace netmon
