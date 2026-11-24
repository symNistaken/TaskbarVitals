// Unit and end-to-end tests for the local read-only JSON snapshot builder and named pipe server.
#include "../src/live_api_json.h"
#include "../src/live_api_pipe.h"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string ReadPipeOnce(const std::wstring& pipeName) {
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 25; ++attempt) {
        h = CreateFileW(pipeName.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) break;
        WaitNamedPipeW(pipeName.c_str(), 100);
        Sleep(10);
    }
    assert(h != INVALID_HANDLE_VALUE);
    std::string out;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) {
        out.append(buf, n);
    }
    CloseHandle(h);
    return out;
}

}  // namespace

int main() {
    using namespace live_api_json;

    // Pure JSON builder: disabled metrics emit null; device array matches --json format.
    {
        DeviceStatus mouse;
        mouse.key = L"logi:1234";
        mouse.name = L"Superlight 2";
        mouse.via = L"2.4 GHz receiver";
        mouse.kind = DeviceKind::Mouse;
        mouse.percent = 74;
        mouse.connected = true;
        mouse.millivolts = 3980;
        mouse.graceSec = 1800;

        SystemMetricsSnapshot off;
        const std::string jsonOff = BuildLiveSnapshotJson({ mouse }, off);
        assert(jsonOff.back() == '\n');
        assert(jsonOff.find("\"version\":1") != std::string::npos);
        assert(jsonOff.find("\"key\":\"logi:1234\"") != std::string::npos);
        assert(jsonOff.find("\"cpuPercent\":null") != std::string::npos);
        assert(jsonOff.find("\"gpuName\":null") != std::string::npos);
        assert(jsonOff.find("\"laptopBatteryPercent\":null") != std::string::npos);
        assert(jsonOff.find("\"netRxBps\":null") != std::string::npos);

        SystemMetricsSnapshot on;
        on.showCpu = true;
        on.cpuPercent = 27;
        on.showCpuTemp = true;
        on.cpuTempC = 64;
        on.showCpuWatts = true;
        on.cpuWatts = 38;
        on.showRam = true;
        on.ramPercent = 52;
        on.ramUsedGb = 16.46;
        on.ramTotalGb = 32.0;
        on.gpuAvailable = true;
        on.gpuName = L"AMD Radeon \"RX\"";
        on.showGpu = true;
        on.gpuPercent = 91;
        on.showGpuTemp = true;
        on.gpuTempC = 71;
        on.showGpuWatts = true;
        on.gpuWatts = 195;
        on.showVram = true;
        on.vramPercent = 48;
        on.vramUsedGb = 3.84;
        on.vramTotalGb = 8.0;
        on.showLaptopBattery = true;
        on.laptopBatteryPresent = true;
        on.laptopBatteryPercent = 82;
        on.laptopBatteryCharging = false;
        on.laptopBatteryFull = false;
        on.laptopBatteryRemainingSec = 7200;
        on.showNetwork = true;
        on.netValid = true;
        on.netRxBps = 123456;
        on.netTxBps = 7890;

        const std::string jsonOn = BuildLiveSnapshotJson({ mouse }, on);
        assert(jsonOn.find("\"cpuPercent\":27") != std::string::npos);
        assert(jsonOn.find("\"cpuTempC\":64") != std::string::npos);
        assert(jsonOn.find("\"cpuWatts\":38") != std::string::npos);
        assert(jsonOn.find("\"ramUsedGb\":16.5") != std::string::npos);
        assert(jsonOn.find("\"ramTotalGb\":32.0") != std::string::npos);
        assert(jsonOn.find("\"gpuName\":\"AMD Radeon \\\"RX\\\"\"") != std::string::npos);
        assert(jsonOn.find("\"gpuWatts\":195") != std::string::npos);
        assert(jsonOn.find("\"laptopBatteryPercent\":82") != std::string::npos);
        assert(jsonOn.find("\"laptopBatteryCharging\":false") != std::string::npos);
        assert(jsonOn.find("\"laptopBatteryRemainingSec\":7200") != std::string::npos);
        assert(jsonOn.find("\"netRxBps\":123456") != std::string::npos);
        assert(jsonOn.find("\"netTxBps\":7890") != std::string::npos);
    }

    // End-to-end Win32 named pipe test on an isolated per-process pipe name.
    {
        const std::wstring testPipe = L"\\\\.\\pipe\\TaskbarVitalsTest_" + std::to_wstring(GetCurrentProcessId());
        SystemMetricsSnapshot snap;
        snap.showCpu = true;
        snap.cpuPercent = 12;
        const std::string payload1 = BuildLiveSnapshotJson({}, snap);
        live_api_pipe::UpdateSnapshot(payload1);
        assert(live_api_pipe::StartServer(testPipe.c_str()));

        // Opening for write access must fail on both outbound mode and FILE_GENERIC_READ ACL.
        HANDLE writeAttempt = CreateFileW(testPipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        assert(writeAttempt == INVALID_HANDLE_VALUE);

        // Wait briefly if needed while the server completes DisconnectNamedPipe between checks, then read.
        if (!WaitNamedPipeW(testPipe.c_str(), 1000)) assert(false);
        const std::string got1 = ReadPipeOnce(testPipe);
        assert(got1 == payload1);

        // Update snapshot and verify a second client receives the updated JSON cleanly.
        snap.cpuPercent = 88;
        const std::string payload2 = BuildLiveSnapshotJson({}, snap);
        live_api_pipe::UpdateSnapshot(payload2);
        assert(WaitNamedPipeW(testPipe.c_str(), 1000));
        const std::string got2 = ReadPipeOnce(testPipe);
        assert(got2 == payload2);

        live_api_pipe::Stop();
    }

    std::puts("Live API JSON & named pipe tests passed");
    return 0;
}
