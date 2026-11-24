#pragma once
// Storage / SSD / HDD monitor: drive temperature (via IOCTL_STORAGE_QUERY_PROPERTY, no admin required),
// busy utilization and read/write throughput (PDH), and free/total capacity (GetDiskFreeSpaceExW).
#include <algorithm>
#include <cstdint>
#include <string>

namespace diskmon {

// Colour thresholds matching typical NVMe/SATA thermal specs: 0 normal, 1 elevated (>=65 C), 2 high (>=75 C).
inline int HeatLevel(int celsius) { return celsius >= 75 ? 2 : celsius >= 65 ? 1 : 0; }

// Clamps Windows' "% Disk Time" counter (which can briefly exceed 100 on multi-queue NVMe) to 0..100.
inline int ClampPercent(double pct) {
    if (pct < 0.0) return -1;
    int rounded = (int)(pct + 0.5);
    return std::min(100, std::max(0, rounded));
}

// Formats disk capacity cleanly ("482 GB" or "1.4 TB").
inline std::wstring FormatCapacityGb(double gb) {
    if (gb <= 0.0) return L"0 GB";
    wchar_t buf[32]{};
    if (gb >= 1024.0) swprintf(buf, 32, L"%.1f TB", gb / 1024.0);
    else swprintf(buf, 32, L"%.0f GB", gb);
    return buf;
}

struct Reading {
    bool available = false;
    std::wstring name;
    std::wstring driveLetter;
    int tempC = -1;
    int usePct = -1;
    double readMBs = 0.0;
    double writeMBs = 0.0;
    double freeGb = 0.0;
    double totalGb = 0.0;
    bool isSsd = true;
};

Reading Sample(bool wantTemp, bool wantUse);
void Stop();

}  // namespace diskmon
