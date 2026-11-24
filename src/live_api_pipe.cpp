#include "live_api_pipe.h"

#include <windows.h>
#include <sddl.h>

#include <string>
#include <utility>

namespace live_api_pipe {
namespace {

SRWLOCK g_dataLock = SRWLOCK_INIT;
std::string g_snapshotJson = "{\"version\":1,\"devices\":[],\"system\":{}}\n";

std::wstring g_pipeName;
HANDLE g_stopEvt = nullptr;
HANDLE g_readyEvt = nullptr;
HANDLE g_thread = nullptr;
bool g_pipeCreated = false;

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    std::wstring out;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD n = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &n);
        std::string buf(n, '\0');
        if (n && GetTokenInformation(token, TokenUser, buf.data(), n, &n)) {
            LPWSTR s = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER*>(buf.data())->User.Sid, &s)) {
                out = s;
                LocalFree(s);
            }
        }
        CloseHandle(token);
    }
    return out;
}

bool BuildCurrentUserReadOnlySa(SECURITY_ATTRIBUTES& sa) {
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return false;
    // Administrators & SYSTEM keep full control; the signed-in user gets FILE_GENERIC_READ only (connect + read).
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00120089;;;" + sid + L")";
    sa = SECURITY_ATTRIBUTES{ sizeof(sa), nullptr, FALSE };
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(
               sddl.c_str(), SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr) != FALSE;
}

std::string CopySnapshot() {
    AcquireSRWLockShared(&g_dataLock);
    std::string copy = g_snapshotJson;
    ReleaseSRWLockShared(&g_dataLock);
    return copy;
}

bool WriteAllOverlapped(HANDLE pipe, HANDLE ovEvt, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ResetEvent(ovEvt);
        OVERLAPPED ov{};
        ov.hEvent = ovEvt;
        const DWORD chunk = static_cast<DWORD>(data.size() - sent);
        DWORD written = 0;
        if (!WriteFile(pipe, data.data() + sent, chunk, &written, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            HANDLE waits[2] = { g_stopEvt, ovEvt };
            const DWORD w = WaitForMultipleObjects(2, waits, FALSE, 1500);
            if (w != WAIT_OBJECT_0 + 1) {
                CancelIoEx(pipe, &ov);
                GetOverlappedResult(pipe, &ov, &written, TRUE);
                return false;
            }
            if (!GetOverlappedResult(pipe, &ov, &written, FALSE)) return false;
        }
        if (written == 0) return false;
        sent += written;
    }
    return true;
}

DWORD WINAPI ServerThread(LPVOID) {
    SECURITY_ATTRIBUTES sa{};
    if (!BuildCurrentUserReadOnlySa(sa)) {
        g_pipeCreated = false;
        if (g_readyEvt) SetEvent(g_readyEvt);
        return 1;
    }

    HANDLE pipe = CreateNamedPipeW(
        g_pipeName.c_str(),
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1,
        65536,
        0,
        1000,
        &sa);
    LocalFree(sa.lpSecurityDescriptor);
    sa.lpSecurityDescriptor = nullptr;

    if (pipe == INVALID_HANDLE_VALUE) {
        g_pipeCreated = false;
        if (g_readyEvt) SetEvent(g_readyEvt);
        return 2;
    }

    HANDLE ovEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ovEvt) {
        CloseHandle(pipe);
        g_pipeCreated = false;
        if (g_readyEvt) SetEvent(g_readyEvt);
        return 3;
    }

    g_pipeCreated = true;
    if (g_readyEvt) SetEvent(g_readyEvt);

    while (WaitForSingleObject(g_stopEvt, 0) != WAIT_OBJECT_0) {
        ResetEvent(ovEvt);
        OVERLAPPED ov{};
        ov.hEvent = ovEvt;
        bool connected = ConnectNamedPipe(pipe, &ov) != FALSE;
        if (!connected) {
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = { g_stopEvt, ovEvt };
                const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (w == WAIT_OBJECT_0) {
                    CancelIoEx(pipe, &ov);
                    DWORD dummy = 0;
                    GetOverlappedResult(pipe, &ov, &dummy, TRUE);
                    break;
                }
                if (w == WAIT_OBJECT_0 + 1) {
                    DWORD dummy = 0;
                    connected = GetOverlappedResult(pipe, &ov, &dummy, FALSE) != FALSE;
                }
            }
        }
        if (connected && WaitForSingleObject(g_stopEvt, 0) != WAIT_OBJECT_0) {
            const std::string payload = CopySnapshot();
            if (WriteAllOverlapped(pipe, ovEvt, payload) &&
                WaitForSingleObject(g_stopEvt, 0) != WAIT_OBJECT_0) {
                FlushFileBuffers(pipe);
            }
        }
        DisconnectNamedPipe(pipe);
    }

    CloseHandle(ovEvt);
    CloseHandle(pipe);
    return 0;
}

}  // namespace

void UpdateSnapshot(std::string utf8Json) {
    if (utf8Json.empty()) return;
    if (utf8Json.back() != '\n') utf8Json.push_back('\n');
    AcquireSRWLockExclusive(&g_dataLock);
    g_snapshotJson = std::move(utf8Json);
    ReleaseSRWLockExclusive(&g_dataLock);
}

bool StartServer(const wchar_t* pipeName) {
    const std::wstring target = pipeName && *pipeName ? pipeName : kDefaultPipeName;
    if (g_thread) {
        if (g_pipeName == target && WaitForSingleObject(g_thread, 0) == WAIT_TIMEOUT) return true;
        Stop();
    }
    g_pipeName = target;
    g_pipeCreated = false;
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_readyEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvt || !g_readyEvt) {
        Stop();
        return false;
    }
    g_thread = CreateThread(nullptr, 0, ServerThread, nullptr, 0, nullptr);
    if (!g_thread) {
        Stop();
        return false;
    }
    HANDLE readyWaits[2] = { g_readyEvt, g_thread };
    WaitForMultipleObjects(2, readyWaits, FALSE, 5000);
    if (!g_pipeCreated) {
        Stop();
        return false;
    }
    return true;
}

void Stop() {
    if (g_stopEvt) SetEvent(g_stopEvt);
    if (g_thread) {
        CancelSynchronousIo(g_thread);
        if (WaitForSingleObject(g_thread, 3000) == WAIT_TIMEOUT) TerminateThread(g_thread, 0);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_readyEvt) { CloseHandle(g_readyEvt); g_readyEvt = nullptr; }
    if (g_stopEvt) { CloseHandle(g_stopEvt); g_stopEvt = nullptr; }
    g_pipeCreated = false;
    g_pipeName.clear();
}

}  // namespace live_api_pipe
