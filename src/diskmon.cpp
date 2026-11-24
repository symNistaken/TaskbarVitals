#include "diskmon.h"

#include <windows.h>
#include <winioctl.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace diskmon {
namespace {

template <class F>
F Proc(HMODULE m, const char* name) {
    return m ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}

// ---- physical drive discovery & SMART/NVMe temperature via StorageDeviceTemperatureProperty ----
bool g_driveChecked = false;
int g_physDriveNum = -1;
std::wstring g_driveLetter = L"C:";
std::wstring g_driveName;
bool g_driveIsSsd = true;
ULONGLONG g_lastTempTick = 0;
int g_cachedTemp = -1;
double g_cachedFreeGb = 0.0, g_cachedTotalGb = 0.0;

std::wstring SystemDriveRoot() {
    wchar_t sys[MAX_PATH]{};
    if (GetSystemDirectoryW(sys, ARRAYSIZE(sys)) >= 2 && sys[1] == L':') {
        return std::wstring(1, sys[0]) + L":\\";
    }
    return L"C:\\";
}

std::wstring TrimAsciiToWide(const char* s) {
    if (!s) return L"";
    std::string raw(s);
    while (!raw.empty() && std::isspace((unsigned char)raw.front())) raw.erase(raw.begin());
    while (!raw.empty() && std::isspace((unsigned char)raw.back())) raw.pop_back();
    std::wstring out;
    out.reserve(raw.size());
    for (unsigned char c : raw)
        if (c >= 0x20 && c < 0x7F) out.push_back((wchar_t)c);
    return out;
}

bool QueryDriveMeta(int driveNum, std::wstring& modelOut, bool& isSsdOut) {
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(driveNum);
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> buf(1024, 0);
    DWORD bytes = 0;
    bool ok = false;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf.data(), (DWORD)buf.size(), &bytes, nullptr) &&
        bytes >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        auto* desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
        std::wstring vendor = (desc->VendorIdOffset && desc->VendorIdOffset < bytes)
                                  ? TrimAsciiToWide((const char*)(buf.data() + desc->VendorIdOffset))
                                  : L"";
        std::wstring prod = (desc->ProductIdOffset && desc->ProductIdOffset < bytes)
                                ? TrimAsciiToWide((const char*)(buf.data() + desc->ProductIdOffset))
                                : L"";
        if (!vendor.empty() && !prod.empty() && prod.find(vendor) == std::wstring::npos) modelOut = vendor + L" " + prod;
        else modelOut = !prod.empty() ? prod : vendor;
        ok = true;
    }

    q.PropertyId = StorageDeviceSeekPenaltyProperty;
    q.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR seek{};
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &seek, sizeof(seek), &bytes, nullptr) &&
        bytes >= sizeof(seek)) {
        isSsdOut = !seek.IncursSeekPenalty;
    }
    CloseHandle(h);
    return ok;
}

bool QueryDriveTemp(int driveNum, int& tempOut) {
    std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(driveNum);
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = StorageDeviceTemperatureProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> buf(sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR) + sizeof(STORAGE_TEMPERATURE_INFO) * 16, 0);
    DWORD bytes = 0;
    bool ok = false;
    if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf.data(), (DWORD)buf.size(), &bytes, nullptr) &&
        bytes >= sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR)) {
        auto* desc = reinterpret_cast<const STORAGE_TEMPERATURE_DATA_DESCRIPTOR*>(buf.data());
        for (WORD i = 0; i < desc->InfoCount; ++i) {
            size_t need = offsetof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo) +
                          (size_t)(i + 1) * sizeof(STORAGE_TEMPERATURE_INFO);
            if (need > bytes) break;
            SHORT t = desc->TemperatureInfo[i].Temperature;
            if (t != (SHORT)STORAGE_TEMPERATURE_VALUE_NOT_REPORTED && t > 0 && t <= 125) {
                tempOut = (int)t;
                ok = true;
                break;
            }
        }
    }
    CloseHandle(h);
    return ok;
}

void EnsureDrive() {
    if (g_driveChecked) return;
    g_driveChecked = true;
    std::wstring root = SystemDriveRoot();
    g_driveLetter = root.substr(0, 2);

    std::wstring volPath = L"\\\\.\\" + g_driveLetter;
    HANDLE vol = CreateFileW(volPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (vol != INVALID_HANDLE_VALUE) {
        BYTE buf[sizeof(VOLUME_DISK_EXTENTS) + sizeof(DISK_EXTENT) * 4]{};
        DWORD bytes = 0;
        if (DeviceIoControl(vol, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf, sizeof(buf), &bytes, nullptr) &&
            bytes >= sizeof(VOLUME_DISK_EXTENTS)) {
            auto* ext = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf);
            if (ext->NumberOfDiskExtents >= 1) g_physDriveNum = (int)ext->Extents[0].DiskNumber;
        }
        CloseHandle(vol);
    }

    int temp = -1;
    if (g_physDriveNum >= 0) {
        QueryDriveMeta(g_physDriveNum, g_driveName, g_driveIsSsd);
        if (!QueryDriveTemp(g_physDriveNum, temp)) {
            // Fall back across other drives only if primary doesn't expose temperature directly.
            for (int n = 0; n < 8; ++n) {
                if (n == g_physDriveNum) continue;
                int probe = -1;
                if (QueryDriveTemp(n, probe)) {
                    g_physDriveNum = n;
                    QueryDriveMeta(n, g_driveName, g_driveIsSsd);
                    temp = probe;
                    break;
                }
            }
        }
    } else {
        for (int n = 0; n < 8; ++n) {
            std::wstring name;
            bool ssd = true;
            if (QueryDriveMeta(n, name, ssd)) {
                g_physDriveNum = n;
                g_driveName = name;
                g_driveIsSsd = ssd;
                break;
            }
        }
    }
    if (g_driveName.empty()) g_driveName = g_driveIsSsd ? L"NVMe / SSD" : L"Disk";
    g_cachedTemp = temp;
    g_lastTempTick = temp >= 0 ? GetTickCount64() : 0;
}

// ---- PDH counters (\PhysicalDisk(_Total)) ----
using OpenQueryFn = PDH_STATUS(WINAPI*)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
using AddCounterFn = PDH_STATUS(WINAPI*)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
using CollectFn = PDH_STATUS(WINAPI*)(PDH_HQUERY);
using ValueFn = PDH_STATUS(WINAPI*)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);
using CloseQueryFn = PDH_STATUS(WINAPI*)(PDH_HQUERY);

HMODULE g_pdh = nullptr;
CollectFn g_collect = nullptr;
ValueFn g_value = nullptr;
CloseQueryFn g_close = nullptr;
PDH_HQUERY g_query = nullptr;
PDH_HCOUNTER g_diskTime = nullptr, g_readBytes = nullptr, g_writeBytes = nullptr;
bool g_collectedOnce = false;
bool g_pdhFailed = false;

bool StartCounters() {
    if (g_query) return true;
    if (g_pdhFailed) return false;
    if (!g_pdh) g_pdh = LoadLibraryExW(L"pdh.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto open = Proc<OpenQueryFn>(g_pdh, "PdhOpenQueryW");
    auto add = Proc<AddCounterFn>(g_pdh, "PdhAddEnglishCounterW");
    g_collect = Proc<CollectFn>(g_pdh, "PdhCollectQueryData");
    g_value = Proc<ValueFn>(g_pdh, "PdhGetFormattedCounterValue");
    g_close = Proc<CloseQueryFn>(g_pdh, "PdhCloseQuery");
    if (!open || !add || !g_collect || !g_value || !g_close || open(nullptr, 0, &g_query) != ERROR_SUCCESS) {
        g_query = nullptr;
        g_pdhFailed = true;
        return false;
    }
    if (add(g_query, L"\\PhysicalDisk(_Total)\\% Disk Time", 0, &g_diskTime) != ERROR_SUCCESS) {
        g_close(g_query);
        g_query = nullptr;
        g_pdhFailed = true;
        return false;
    }
    add(g_query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &g_readBytes);
    add(g_query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &g_writeBytes);
    g_collectedOnce = false;
    return true;
}

double ReadDouble(PDH_HCOUNTER c) {
    if (!c) return -1.0;
    PDH_FMT_COUNTERVALUE v{};
    if (g_value(c, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &v) != ERROR_SUCCESS) return -1.0;
    if (v.CStatus != PDH_CSTATUS_VALID_DATA && v.CStatus != PDH_CSTATUS_NEW_DATA) return -1.0;
    return v.doubleValue;
}

}  // namespace

Reading Sample(bool wantTemp, bool wantUse) {
    Reading r;
    EnsureDrive();
    r.available = g_physDriveNum >= 0 || wantUse;
    r.name = g_driveName;
    r.driveLetter = g_driveLetter;
    r.isSsd = g_driveIsSsd;

    ULARGE_INTEGER freeAvail{}, totalBytes{};
    if (GetDiskFreeSpaceExW(SystemDriveRoot().c_str(), &freeAvail, &totalBytes, nullptr) && totalBytes.QuadPart > 0) {
        g_cachedFreeGb = freeAvail.QuadPart / 1073741824.0;
        g_cachedTotalGb = totalBytes.QuadPart / 1073741824.0;
    }
    r.freeGb = g_cachedFreeGb;
    r.totalGb = g_cachedTotalGb;

    if (wantTemp && g_physDriveNum >= 0) {
        ULONGLONG now = GetTickCount64();
        if (!g_lastTempTick || now - g_lastTempTick >= 10000) {
            int t = -1;
            if (QueryDriveTemp(g_physDriveNum, t)) g_cachedTemp = t;
            g_lastTempTick = now;
        }
        r.tempC = g_cachedTemp;
    }

    if (wantUse && StartCounters() && g_collect(g_query) == ERROR_SUCCESS) {
        if (g_collectedOnce) {
            r.usePct = ClampPercent(ReadDouble(g_diskTime));
            double rd = ReadDouble(g_readBytes);
            double wr = ReadDouble(g_writeBytes);
            r.readMBs = rd > 0.0 ? rd / 1048576.0 : 0.0;
            r.writeMBs = wr > 0.0 ? wr / 1048576.0 : 0.0;
        }
        g_collectedOnce = true;
    }
    return r;
}

void Stop() {
    if (g_query && g_close) g_close(g_query);
    g_query = nullptr;
    g_diskTime = g_readBytes = g_writeBytes = nullptr;
    g_collectedOnce = false;
    g_pdhFailed = false;
    g_driveChecked = false;
    g_lastTempTick = 0;
    g_cachedTemp = -1;
}

}  // namespace diskmon
