// TaskbarVitalsTemp.exe: reads the CPU package temperature for Taskbar Vitals. See src/cputemp.h for the design.
//
//   TaskbarVitalsTemp.exe                 run: read the temperature every 2 s and publish it (started by the task)
//   TaskbarVitalsTemp.exe --install [--user SID]   copy itself into Program Files and register the sign-in task
//   TaskbarVitalsTemp.exe --remove        stop, delete the task and the copy
//
// It always runs elevated (see the manifest). On Intel CPUs it reads two MSRs through PawnIO IntelMSR; on AMD
// Ryzen (Family 17h-1Ah) it reads SMN THM_TCON_CUR_TMP (0x00059800) through PawnIO AMDFamily17. It takes
// no input except the stop event; the app (not elevated) only reads its shared memory block.
#include <windows.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cpuid.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../src/cputemp.h"

namespace {

using namespace cputemp;

// Development aids, off in the real build: -DHELPER_LOG writes what the helper does to %TEMP%\TaskbarVitalsTemp.log;
// -DHELPER_NOADMIN lets it run without administrator rights (the driver is then out of reach, the rest works).
#ifdef HELPER_LOG
void Log(const wchar_t* what, long long a = 0, long long b = 0) {
    wchar_t path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, path);
    std::wstring file = std::wstring(path) + L"TaskbarVitalsTemp.log";
    FILE* f = _wfopen(file.c_str(), L"a");
    if (!f) return;
    fwprintf(f, L"%llu %ls %lld %lld\n", (unsigned long long)GetTickCount64(), what, a, b);
    fclose(f);
}
#else
inline void Log(const wchar_t*, long long = 0, long long = 0) {}
#endif

constexpr int kIntelModuleResource = 200;  // IntelMSR.bin, RCDATA (res/pawnio/IntelMSR.bin)
constexpr int kAmdModuleResource = 201;    // AMDFamily17.bin, RCDATA (res/pawnio/AMDFamily17.bin)
constexpr DWORD kPollMs = 2000;
constexpr ULONGLONG kAppGoneMs = 30000;     // quit this long after the app's window disappeared
constexpr ULONGLONG kRetryOpenMs = 10000;   // try the driver again this often while it is missing

enum class CpuVendor { Unknown, Intel, Amd };

void DetectCpu(CpuVendor& vendor, std::string& brand) {
    vendor = CpuVendor::Unknown;
    brand.clear();
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        char v[13]{};
        memcpy(v + 0, &ebx, 4);
        memcpy(v + 4, &edx, 4);
        memcpy(v + 8, &ecx, 4);
        if (strcmp(v, "GenuineIntel") == 0) vendor = CpuVendor::Intel;
        else if (strcmp(v, "AuthenticAMD") == 0) vendor = CpuVendor::Amd;
    }
    unsigned int maxExt = __get_cpuid_max(0x80000000, nullptr);
    if (maxExt >= 0x80000004) {
        char b[49]{};
        for (int i = 0; i < 3; ++i) {
            if (__get_cpuid(0x80000002u + i, &eax, &ebx, &ecx, &edx)) {
                memcpy(b + i * 16 + 0, &eax, 4);
                memcpy(b + i * 16 + 4, &ebx, 4);
                memcpy(b + i * 16 + 8, &ecx, 4);
                memcpy(b + i * 16 + 12, &edx, 4);
            }
        }
        brand = b;
    }
}

// ---- small helpers --------------------------------------------------------------------------------

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = p;
    CoTaskMemFree(p);
    return out;
}

std::wstring InstallDir() { return KnownFolder(FOLDERID_ProgramFiles) + L"\\" + kInstallFolder; }
std::wstring InstalledExe() { return InstallDir() + L"\\" + kHelperFile; }

std::wstring SelfPath() {
    wchar_t buf[MAX_PATH * 2] = {};
    GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf) - 1);
    return buf;
}

bool IsElevated() {
    HANDLE token = nullptr;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    bool yes = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
               GetTokenInformation(token, TokenElevation, &e, sizeof(e), &n) && e.TokenIsElevated;
    if (token) CloseHandle(token);
    return yes;
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

bool SettingOn() {
    DWORD v = 0, size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", kSettingValue, RRF_RT_REG_DWORD, nullptr, &v, &size) ==
               ERROR_SUCCESS && v == 1;
}

DWORD RunHidden(std::wstring commandLine) {
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code;
}

std::wstring Schtasks() {
    wchar_t dir[MAX_PATH] = {};
    GetSystemDirectoryW(dir, ARRAYSIZE(dir));
    return std::wstring(L"\"") + dir + L"\\schtasks.exe\"";
}

// ---- the PawnIO driver ----------------------------------------------------------------------------

std::wstring PawnIoDir() {
    wchar_t buf[MAX_PATH] = {};
    DWORD size = sizeof(buf) - sizeof(wchar_t);
    std::wstring dir;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO", L"InstallLocation",
                     RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
        dir = buf;
    if (dir.empty()) dir = KnownFolder(FOLDERID_ProgramFiles) + L"\\PawnIO";
    while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    return dir;
}

class PawnIo {
  public:
    ~PawnIo() { Close(); }

    // Loads PawnIOLib from where PawnIO is installed, opens a handle and loads the CPU vendor module.
    Status Open(CpuVendor vendor) {
        Close();
        if (vendor == CpuVendor::Unknown) return Status::Unsupported;
        vendor_ = vendor;
        lib_ = LoadLibraryExW((PawnIoDir() + L"\\PawnIOLib.dll").c_str(), nullptr,
                              LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
        if (!lib_) return Status::NoPawnIo;
        auto open = Fn<OpenFn>("pawnio_open");
        auto load = Fn<LoadFn>("pawnio_load");
        execute_ = Fn<ExecuteFn>("pawnio_execute");
        close_ = Fn<CloseFn>("pawnio_close");
        if (!open || !load || !execute_ || !close_) return Fail(Status::NoPawnIo);
        if (FAILED(open(&handle_))) return Fail(Status::NoPawnIo);
        int resId = vendor == CpuVendor::Amd ? kAmdModuleResource : kIntelModuleResource;
        HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
        HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
        const UCHAR* blob = g ? (const UCHAR*)LockResource(g) : nullptr;
        if (!blob) return Fail(Status::ModuleFailed);
        HRESULT hr = load(handle_, blob, SizeofResource(nullptr, r));
        if (FAILED(hr)) return Fail(hr == HRESULT_FROM_NT(0xC00000BB) ? Status::Unsupported : Status::ModuleFailed);
        if (vendor == CpuVendor::Amd) {
            pciMutex_ = CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI");
            if (!pciMutex_) pciMutex_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, L"Global\\Access_PCI");
        }
        return Status::Ok;
    }

    bool ReadMsr(std::uint32_t msr, std::uint64_t& value) {
        // Only the two registers the helper exists for; the module would refuse anything outside its own list anyway.
        if (!handle_ || vendor_ != CpuVendor::Intel ||
            (msr != kMsrPackageThermStatus && msr != kMsrTemperatureTarget)) return false;
        ULONG64 in = msr, out = 0;
        SIZE_T returned = 0;
        if (FAILED(execute_(handle_, "ioctl_read_msr", &in, 1, &out, 1, &returned))) return false;
        value = out;
        return true;
    }

    bool ReadAmdSmn(std::uint32_t offset, std::uint32_t& value) {
        if (!handle_ || vendor_ != CpuVendor::Amd || offset != kAmdSmnThmTconCurTmp) return false;
        bool locked = false;
        if (pciMutex_) {
            DWORD w = WaitForSingleObject(pciMutex_, 100);
            if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) locked = true;
            else if (w == WAIT_TIMEOUT) return false;
        }
        ULONG64 in = offset, out = 0;
        SIZE_T returned = 0;
        HRESULT hr = execute_(handle_, "ioctl_read_smn", &in, 1, &out, 1, &returned);
        if (locked) ReleaseMutex(pciMutex_);
        if (FAILED(hr) || returned < 1) return false;
        value = static_cast<std::uint32_t>(out);
        return true;
    }

    void Close() {
        if (pciMutex_) { CloseHandle(pciMutex_); pciMutex_ = nullptr; }
        if (handle_ && close_) close_(handle_);
        handle_ = nullptr;
        if (lib_) FreeLibrary(lib_);
        lib_ = nullptr;
    }

    bool IsOpen() const { return handle_ != nullptr; }

  private:
    using OpenFn = HRESULT(WINAPI*)(PHANDLE);
    using LoadFn = HRESULT(WINAPI*)(HANDLE, const UCHAR*, SIZE_T);
    using ExecuteFn = HRESULT(WINAPI*)(HANDLE, PCSTR, const ULONG64*, SIZE_T, PULONG64, SIZE_T, PSIZE_T);
    using CloseFn = HRESULT(WINAPI*)(HANDLE);

    template <class F>
    F Fn(const char* name) {
        return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(lib_, name)));
    }
    Status Fail(Status s) {
        Close();
        return s;
    }

    CpuVendor vendor_ = CpuVendor::Unknown;
    HMODULE lib_ = nullptr;
    HANDLE handle_ = nullptr;
    HANDLE pciMutex_ = nullptr;
    ExecuteFn execute_ = nullptr;
    CloseFn close_ = nullptr;
};

// ---- publishing ---------------------------------------------------------------------------------------

// A named object that only administrators and SYSTEM control; the signed-in user gets `userRights` (read access to
// the block, or the right to signal the stop event). The label lets the (medium integrity) app use it.
bool SecurityFor(const std::wstring& userSid, const wchar_t* userRights, SECURITY_ATTRIBUTES& sa) {
    std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;" + std::wstring(userRights) + L";;;" + userSid + L")S:(ML;;NW;;;ME)";
    sa = SECURITY_ATTRIBUTES{ sizeof(sa), nullptr, FALSE };
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sa.lpSecurityDescriptor, nullptr);
}

int RunReader() {
    Log(L"start");
#ifndef HELPER_NOADMIN
    if (!IsElevated()) { Log(L"not elevated"); return 2; }
#endif
    HANDLE once = CreateMutexW(nullptr, FALSE, kHelperMutexName);
    DWORD onceErr = GetLastError();
    Log(L"mutex", once != nullptr, onceErr);
    if (!once || onceErr == ERROR_ALREADY_EXISTS) return 0;  // already running
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);

    const std::wstring sid = CurrentUserSid();
    SECURITY_ATTRIBUTES mapSa{}, evSa{};
    if (sid.empty() || !SecurityFor(sid, L"GR", mapSa) || !SecurityFor(sid, L"0x00100002", evSa)) { Log(L"security failed", GetLastError()); return 3; }
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &mapSa, PAGE_READWRITE, 0, sizeof(Shared), kMappingName);
    DWORD mapErr = GetLastError();
    Log(L"mapping", mapping != nullptr, mapErr);
    if (!mapping || mapErr == ERROR_ALREADY_EXISTS) return 4;  // someone else made the name: do not use it
    HANDLE stop = CreateEventW(&evSa, TRUE, FALSE, kStopEventName);
    DWORD stopErr = GetLastError();
    Log(L"stop event", stop != nullptr, stopErr);
    if (!stop || stopErr == ERROR_ALREADY_EXISTS) return 4;
    auto* shared = (Shared*)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(Shared));
    if (!shared) { Log(L"map view failed", GetLastError()); return 4; }

    auto publish = [&](Status status, int tempC, int tjMax) {
        shared->status = (std::uint32_t)status;
        shared->tempC = tempC;
        shared->tjMax = tjMax;
        shared->helperVersion = 1;
        shared->version = kVersion;
        shared->magic = kMagic;
        shared->tick = GetTickCount64();  // last: a reader sees fresh data only once the rest is in place
    };
    publish(Status::Starting, -1, 0);

    CpuVendor vendor = CpuVendor::Unknown;
    std::string brand;
    DetectCpu(vendor, brand);
    const int tctlOffset = vendor == CpuVendor::Amd ? AmdTctlOffset(brand) : 0;

    PawnIo io;
    int tjMax = 100;
    bool haveTarget = false;
    ULONGLONG lastApp = GetTickCount64(), lastOpen = 0;
    do {
        ULONGLONG now = GetTickCount64();
        if (!SettingOn()) { Log(L"quit: option is off"); break; }  // the option was turned off
        if (FindWindowW(kMainWindowClass, nullptr)) lastApp = now;
        else if (now - lastApp > kAppGoneMs) { Log(L"quit: app is gone"); break; }  // the app is gone

        if (!io.IsOpen() && (lastOpen == 0 || now - lastOpen >= kRetryOpenMs)) {
            lastOpen = now;
            Status s = io.Open(vendor);
            Log(L"driver open", (long long)s, (long long)vendor);
            if (s != Status::Ok) publish(s, -1, 0);
            haveTarget = false;
        }
        if (io.IsOpen()) {
            if (vendor == CpuVendor::Intel) {
                std::uint64_t target = 0, therm = 0;
                if (!haveTarget && io.ReadMsr(kMsrTemperatureTarget, target)) {
                    tjMax = TjMax(target);
                    haveTarget = true;
                }
                int t = haveTarget && io.ReadMsr(kMsrPackageThermStatus, therm) ? PackageTemperature(therm, tjMax) : -1;
                Log(L"reading intel", t, tjMax);
                if (t >= 0) publish(Status::Ok, t, tjMax);
                else publish(haveTarget ? Status::Unsupported : Status::ReadFailed, -1, tjMax);
            } else if (vendor == CpuVendor::Amd) {
                std::uint32_t regVal = 0;
                if (io.ReadAmdSmn(kAmdSmnThmTconCurTmp, regVal)) {
                    int t = AmdPackageTemperature(regVal, tctlOffset);
                    Log(L"reading amd", t, regVal);
                    if (t >= 0) publish(Status::Ok, t, 95);
                    else publish(Status::Unsupported, -1, 0);
                } else {
                    Log(L"reading amd failed");
                    publish(Status::ReadFailed, -1, 0);
                }
            }
        }
    } while ([&] { DWORD w = WaitForSingleObject(stop, kPollMs); if (w != WAIT_TIMEOUT) Log(L"quit: wait ended", w, GetLastError()); return w == WAIT_TIMEOUT; }());
    Log(L"exit");

    publish(Status::Starting, -1, 0);
    shared->magic = 0;  // nothing to show once we are gone
    return 0;
}

// ---- install and remove (elevated, once) ------------------------------------------------------------------

void StopRunningHelper() {
    HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, kStopEventName);
    if (stop) {
        SetEvent(stop);
        CloseHandle(stop);
    }
}

bool DeleteWithRetry(const std::wstring& path) {
    for (int i = 0; i < 20; i++) {
        if (DeleteFileW(path.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        Sleep(500);
    }
    return false;
}

bool WriteUtf16(const std::wstring& path, const std::wstring& text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    const WORD bom = 0xFEFF;
    DWORD n = 0;
    bool ok = WriteFile(f, &bom, sizeof(bom), &n, nullptr) &&
              WriteFile(f, text.data(), (DWORD)(text.size() * sizeof(wchar_t)), &n, nullptr);
    CloseHandle(f);
    return ok;
}

bool SamePath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

int Install(const std::wstring& expectedSid) {
    if (!IsElevated()) return 2;
    const std::wstring sid = CurrentUserSid();
    if (sid.empty() || (!expectedSid.empty() && _wcsicmp(sid.c_str(), expectedSid.c_str()) != 0)) return 3;  // another account approved the prompt

    const std::wstring dir = InstallDir(), target = InstalledExe(), self = SelfPath();
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!SamePath(self, target)) {
        StopRunningHelper();  // a running copy keeps its file locked
        const std::wstring staged = target + L".new";
        bool copied = false;
        for (int i = 0; i < 20 && !copied; i++) {
            copied = CopyFileW(self.c_str(), staged.c_str(), FALSE) && MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (!copied) Sleep(500);
        }
        if (!copied) return 4;
    }
    // The task definition goes through a file in the same protected folder, never through a user-writable one.
    const std::wstring xml = dir + L"\\task.xml";
    if (!WriteUtf16(xml, TaskXml(sid, target))) return 5;
    DWORD code = RunHidden(Schtasks() + L" /Create /TN \"" + kTaskName + L"\" /XML \"" + xml + L"\" /F");
    DeleteWithRetry(xml);
    return code == 0 ? 0 : 5;
}

int Remove() {
    if (!IsElevated()) return 2;
    StopRunningHelper();
    RunHidden(Schtasks() + L" /Delete /TN \"" + kTaskName + L"\" /F");
    const std::wstring dir = InstallDir();
    DeleteWithRetry(InstalledExe());
    DeleteWithRetry(InstalledExe() + L".new");
    RemoveDirectoryW(dir.c_str());
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring mode, user;
    for (int i = 1; argv && i < argc; i++) {
        if (!wcscmp(argv[i], L"--install") || !wcscmp(argv[i], L"--remove")) mode = argv[i];
        else if (!wcscmp(argv[i], L"--user") && i + 1 < argc) user = argv[++i];
    }
    if (argv) LocalFree(argv);
    if (mode == L"--install") return Install(user);
    if (mode == L"--remove") return Remove();
    return RunReader();
}
