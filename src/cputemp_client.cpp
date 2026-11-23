#include "cputemp_client.h"

#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>

#include <string>

namespace cputemp {
namespace {

std::wstring ProgramFiles() {
    wchar_t buf[MAX_PATH] = {};
    return SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, buf) == S_OK ? buf : L"";
}

std::wstring InstalledHelper() { return ProgramFiles() + L"\\" + kInstallFolder + L"\\" + kHelperFile; }

std::wstring OwnFolder() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf) - 1);
    std::wstring p = buf;
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash);
}

std::wstring CurrentUserSid() {
    HANDLE token = nullptr;
    std::wstring out;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD n = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &n);
        std::string buf(n, '\0');
        if (n && GetTokenInformation(token, TokenUser, buf.data(), n, &n)) {
            LPWSTR s = nullptr;
            if (ConvertSidToStringSidW(((TOKEN_USER*)buf.data())->User.Sid, &s)) {
                out = s;
                LocalFree(s);
            }
        }
        CloseHandle(token);
    }
    return out;
}

// Waits for a process while the window messages keep flowing (the UI stays alive behind the Windows prompt).
DWORD WaitPumping(HANDLE process, DWORD timeoutMs) {
    ULONGLONG end = GetTickCount64() + timeoutMs;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        if (now >= end) return WAIT_TIMEOUT;
        DWORD r = MsgWaitForMultipleObjects(1, &process, FALSE, (DWORD)(end - now), QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) return r;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

// Runs the helper elevated with `parameters`; returns its exit code, or -1 if the prompt was refused (-2) or it failed.
int RunElevated(HWND owner, const std::wstring& parameters) {
    const std::wstring exe = OwnFolder() + L"\\" + kHelperFile;
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return -1;
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = parameters.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return GetLastError() == ERROR_CANCELLED ? -2 : -1;
    DWORD code = (DWORD)-1;
    if (WaitPumping(sei.hProcess, 90000) == WAIT_OBJECT_0) GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return (int)code;
}

}  // namespace

Reading ReadShared() {
    Reading r;
    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
    if (!map) return r;
    auto* view = (const Shared*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(Shared));
    if (view) {
        Shared s = *view;
        UnmapViewOfFile(view);
        if (Fresh(s, GetTickCount64())) {
            r.running = true;
            r.status = (Status)s.status;
            if (r.status == Status::Ok && s.tempC >= 0 && s.tempC <= 125) r.tempC = s.tempC;
        }
    }
    CloseHandle(map);
    return r;
}

bool PawnIoInstalled() {
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO", L"InstallLocation",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS && buf[0])
        return GetFileAttributesW((std::wstring(buf) + L"\\PawnIOLib.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
    return GetFileAttributesW((ProgramFiles() + L"\\PawnIO\\PawnIOLib.dll").c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool HelperInstalled() { return GetFileAttributesW(InstalledHelper().c_str()) != INVALID_FILE_ATTRIBUTES; }

InstallResult InstallHelper(HWND owner) {
    int code = RunElevated(owner, L"--install --user " + CurrentUserSid());
    if (code == 0) return InstallResult::Ok;
    return code == -2 ? InstallResult::Cancelled : InstallResult::Failed;
}

bool RemoveHelper(HWND owner) { return RunElevated(owner, L"--remove") == 0; }

void StartHelper() {
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, ARRAYSIZE(sys));
    std::wstring cmd = std::wstring(L"\"") + sys + L"\\schtasks.exe\" /Run /TN \"" + kTaskName + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);  // it finishes on its own
    }
}

void StopHelper() {
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEventName);
    if (stop) {
        SetEvent(stop);
        CloseHandle(stop);
    }
}

}  // namespace cputemp
