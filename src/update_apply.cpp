#include "update_apply.h"
#include "update_win.h"
#include "i18n.h"
#include <shellapi.h>
#include <objbase.h>
#include <vector>

namespace updates {
std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L'\"';
    return out;
}

namespace {
std::wstring Wide(const std::string& text) { return std::wstring(text.begin(), text.end()); }
std::string Ascii(const wchar_t* text) {
    std::string out;
    for (; *text; ++text) { if (*text > 127) return {}; out += (char)*text; }
    return out;
}

bool Launch(const std::wstring& executable, const std::wstring& arguments, bool elevate, HANDLE* process) {
    if (process) *process = nullptr;
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return false;
    SHELLEXECUTEINFOW info{ sizeof(info) };
    // The helper exits after relaunching the app, so finish Shell's launch work before returning.
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpVerb = elevate ? L"runas" : L"open";
    info.lpFile = executable.c_str();
    info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    info.nShow = SW_SHOWNORMAL;
    bool launched = ShellExecuteExW(&info) != FALSE;
    DWORD error = launched ? ERROR_SUCCESS : GetLastError();
    if (launched && process && !info.hProcess) {
        launched = false;
        error = ERROR_INVALID_HANDLE;
    }
    if (launched && process) *process = info.hProcess;
    else if (info.hProcess) CloseHandle(info.hProcess);
    if (SUCCEEDED(com)) CoUninitialize();
    SetLastError(error);
    return launched;
}

void Error(const wchar_t* text) { MessageBoxW(nullptr, T(text), T(L"Wireless Battery update"), MB_OK | MB_ICONINFORMATION); }

void LogResult(DWORD code) {
    wchar_t local[32768];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, ARRAYSIZE(local));
    if (!n || n >= ARRAYSIZE(local)) return;
    std::wstring directory = std::wstring(local) + L"\\WirelessBattery";
    CreateDirectoryW(directory.c_str(), nullptr);
    HANDLE log = CreateFileW((directory + L"\\update.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME time;
    GetSystemTime(&time);
    char line[128];
    int length = wsprintfA(line, "%04u-%02u-%02u %02u:%02u:%02u UTC installer exit=%lu\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, code);
    DWORD written;
    WriteFile(log, line, length, &written, nullptr);
    CloseHandle(log);
}

bool InstalledVersion(const std::wstring& exe, const Version& expected) {
    DWORD ignored = 0, bytes = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
    if (!bytes || bytes > 1024 * 1024) return false;
    std::vector<unsigned char> data(bytes);
    if (!GetFileVersionInfoW(exe.c_str(), 0, bytes, data.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT size = 0;
    return VerQueryValueW(data.data(), L"\\", (void**)&info, &size) && size >= sizeof(*info) &&
        HIWORD(info->dwFileVersionMS) == expected.major && LOWORD(info->dwFileVersionMS) == expected.minor &&
        HIWORD(info->dwFileVersionLS) == expected.patch;
}
}

bool StartApply(HWND owner, const std::wstring& installer, const ReleaseInfo& release, HANDLE* process) {
    auto target = ExecutablePath();
    auto installed = FindInstallation(target);
    if (!installed.valid || !release.CanInstall() || !VerifyFile(installer, release.sha256, release.size)) {
        MessageBoxW(owner, T(L"The update or installation could not be verified. Please use the official release page."),
            T(L"Wireless Battery update"), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    size_t slash = installer.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;
    std::wstring helper = installer.substr(0, slash) + L"\\update-helper.exe";
    if (!CopyFileW(target.c_str(), helper.c_str(), TRUE)) {
        MessageBoxW(owner, T(L"Could not prepare the update helper. Your current version was kept."), T(L"Wireless Battery update"), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    std::wstring arguments = L"--apply-update " + QuoteArgument(installer) + L" " + QuoteArgument(Wide(release.sha256)) + L" " +
        QuoteArgument(std::to_wstring(release.size)) + L" " + QuoteArgument(target) + L" " + QuoteArgument(Wide(VersionText(release.version))) +
        L" " + std::to_wstring(GetCurrentProcessId());
    if (!Launch(helper, arguments, false, process)) {
        DeleteFileW(helper.c_str());
        MessageBoxW(owner, T(L"Could not start the update helper. Your current version was kept."), T(L"Wireless Battery update"), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

int RunApplyMode(int argc, wchar_t** argv) {
    if (argc < 2 || wcscmp(argv[1], L"--apply-update") != 0) return -1;
    if (argc != 8) return 2;
    i18n::Init();  // the helper runs before the normal startup, so its messages need the language too
    std::wstring installer = argv[2], target = argv[5], self = ExecutablePath();
    auto slash = self.find_last_of(L'\\');
    std::wstring root = DownloadRoot();
    if (root.empty() || slash == std::wstring::npos) return 2;
    std::wstring directory = self.substr(0, slash);
    std::wstring prefix = root + L"\\";  // the helper only runs from its own download folder
    if (directory.compare(0, prefix.size(), prefix) != 0 || directory.size() != prefix.size() + 32 ||
        installer != directory + L"\\update-setup.exe" || self != directory + L"\\update-helper.exe") return 2;
    for (size_t i = prefix.size(); i < directory.size(); ++i)
        if (!((directory[i] >= L'0' && directory[i] <= L'9') || (directory[i] >= L'a' && directory[i] <= L'f'))) return 2;
    std::string hash, inputHash = Ascii(argv[3]);
    Version expected;
    if (!ValidDigest("sha256:" + inputHash, hash) || !ParseVersion(Ascii(argv[6]), expected)) return 2;
    wchar_t* end = nullptr;
    auto bytes = wcstoull(argv[4], &end, 10);
    if (!end || *end || !bytes || bytes > MaxDownloadBytes) return 2;
    unsigned long pid = wcstoul(argv[7], &end, 10);
    if (!end || *end || !pid) return 2;
    auto installed = FindInstallation(target);
    if (!installed.valid) return 2;
    HANDLE parent = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    wchar_t parentPath[32768];
    DWORD parentSize = ARRAYSIZE(parentPath);
    if (!parent || !QueryFullProcessImageNameW(parent, 0, parentPath, &parentSize) || _wcsicmp(parentPath, target.c_str()) != 0) {
        if (parent) CloseHandle(parent);
        return 2;
    }
    // Keep the verified installer locked against replacement until setup exits.
    HANDLE guard = CreateFileW(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (guard == INVALID_HANDLE_VALUE || !VerifyFile(installer, hash, bytes)) {
        if (guard != INVALID_HANDLE_VALUE) CloseHandle(guard);
        CloseHandle(parent);
        Error(L"The update installer failed verification. Your current version was kept.");
        return 2;
    }
    std::wstring arguments = L"/SP- /SILENT /NORESTART /RESTARTEXITCODE=3010 /WBUPDATE=1 /DIR=" + QuoteArgument(installed.directory) +
        (installed.allUsers ? L" /ALLUSERS" : L" /CURRENTUSER");
    HANDLE setup = nullptr;
    if (!Launch(installer, arguments, installed.allUsers, &setup) || !setup) {
        CloseHandle(guard);
        CloseHandle(parent);
        Error(L"The update was cancelled or could not start. Your current version was kept.");
        DeleteFileW(installer.c_str());
        return 1;
    }
    WaitForSingleObject(setup, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(setup, &code);
    CloseHandle(setup);
    CloseHandle(guard);
    LogResult(code);
    bool confirmed = code == 0 && InstalledVersion(target, expected);
    bool parentExited = WaitForSingleObject(parent, 0) == WAIT_OBJECT_0;
    if (code == 0 && !parentExited) parentExited = WaitForSingleObject(parent, 10000) == WAIT_OBJECT_0;
    if (code == 3010) Error(L"Windows needs to restart before this update is complete. Your computer will not restart automatically.");
    else if (code != 0) Error(L"The update did not finish. You may need to rerun the installer from the official release page.");
    else if (!confirmed) Error(L"Setup finished, but the new version could not be confirmed. Please check the installed version.");
    // A slow app may finish exiting while the installer/error dialog is visible.
    if (!parentExited) parentExited = WaitForSingleObject(parent, 1000) == WAIT_OBJECT_0;
    CloseHandle(parent);
    bool restarted = false;
    if (parentExited && code != 3010) {
        restarted = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES &&
                    Launch(target, L"", false, nullptr);
        if (!restarted) Error(L"Wireless Battery could not reopen. Please start it manually or repair the installation.");
    } else if (code == 0 && !parentExited) {
        Error(L"The previous instance is still running. Close and reopen Wireless Battery to use the update.");
    }
    DeleteFileW(installer.c_str());
    // This helper is still in use. Never schedule a reboot just for cleanup.
    return confirmed && restarted ? 0 : 1;
}
}
