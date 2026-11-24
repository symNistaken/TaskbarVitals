#pragma once
// Optional Native Win32 Device Dashboard for BatteryHub.
// Provides device cards, detailed analytics, battery intelligence,
// charging history, and device battery consumption comparisons.
#include <windows.h>
#include <string>
#include <vector>

#include "devices.h"
#include "history_model.h"

namespace dashboard {

// Live hardware snapshot displayed in the dashboard's System Vitals tab.
struct VitalsSnapshot {
    int cpuPct = -1;
    int cpuTempC = -1;
    int cpuWatts = -1;
    int ramPct = -1;
    double ramUsedGb = 0.0;
    double ramTotalGb = 0.0;

    bool gpuAvailable = false;
    std::wstring gpuName;
    int gpuPct = -1;
    int gpuTempC = -1;
    int gpuWatts = -1;
    int gpuClockMhz = -1;
    int vramClockMhz = -1;
    int vramPct = -1;
    double vramUsedGb = 0.0;
    double vramTotalGb = 0.0;

    bool diskAvailable = false;
    std::wstring diskName;
    std::wstring diskDrive;
    int diskTempC = -1;
    int diskPct = -1;
    double diskReadMBs = 0.0;
    double diskWriteMBs = 0.0;
    double diskFreeGb = 0.0;
    double diskTotalGb = 0.0;

    bool netAvailable = false;
    std::wstring netDownFull;
    std::wstring netUpFull;
    bool netTesting = false;
    bool netTestDone = false;
    bool netTestFailed = false;
    std::wstring netTestDown;
    std::wstring netTestUp;

    bool batPresent = false;
    int batPct = -1;
    bool batCharging = false;
    bool batAcOnline = false;
    bool batHasWatts = false;
    std::wstring batWattsText;
    std::wstring batRemainingText;
    std::uint32_t batRemainingMwh = 0;
    std::uint32_t batMaxMwh = 0;

    std::vector<int> cpuHist, ramHist, gpuHist, vramHist, diskHist, netHist, batHist;
};

// Shows or restores the dashboard window (`initialTab`: 0 Overview .. 4 System Vitals).
void Show(HWND owner = nullptr, int initialTab = 0);

// Closes the dashboard window if open.
void Close();

// Returns true if the dashboard window is currently open.
bool IsOpen();

// Updates live device data when new poll results arrive.
void UpdateDevices(const std::vector<DeviceStatus>& devices, const battery_history::Store& store);

// Updates live hardware telemetry shown on the System Vitals tab.
void UpdateVitals(const VitalsSnapshot& vitals);

// Registers the callback triggered when "Run speed test" is clicked inside the dashboard.
void SetSpeedTestHandler(void (*fn)());

// Selects a specific device by key and switches to the Details tab.
void SelectDevice(const std::wstring& key);

} // namespace dashboard
