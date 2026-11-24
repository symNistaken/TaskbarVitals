// BigBigWon Armor X Pro (PR #145).
//  On its 2.4G dongle the controller poses as a wired Xbox pad (045E:0B12), so Windows only ever
//  reports "Full". It also keeps a Bluetooth LE link open for the phone app, and that link carries
//  the real level in the standard Battery Level characteristic (0x2A19). The controller is not
//  paired with Windows: pairing would add a second gamepad in games and lock out the phone.
//  The shared advertisement watcher (ble_adverts.cpp, switched to an active scan while the dongle is
//  plugged in: the name "ARMOR-X..." is in the scan response) remembers the controller's address. A
//  background thread connects, reads one byte and closes about once a minute, so the phone can still
//  connect in between; the poll reports the latest reading (marshall.cpp does the LE read).
#include "drivers.h"
#include "armorx_trend.h"
#include "ble_adverts.h"

#include <atomic>
#include <cfgmgr32.h>
#include <cwctype>
#include <mutex>
#include <roapi.h>

namespace {

std::atomic<UINT64> g_address{ 0 };

struct State {
    std::mutex m;
    bool running = false;
    int percent = -1;
    bool charging = false;
    ULONGLONG lastOk = 0, lastAttempt = 0;
    ArmorX::Trend trend;  // the controller sends no charging flag; it is guessed from the trend
};
State g_state;

constexpr ULONGLONG kEveryMs = 60 * 1000;
constexpr ULONGLONG kStaleMs = 10 * 60 * 1000;

// The 2.4 GHz dongle is plugged in (a real Xbox Series pad on USB shares this id, which only costs a scan).
bool DonglePresent() {
    const ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, L"USB", flags) != CR_SUCCESS || !len) return false;
    std::wstring ids(len, L'\0');
    if (CM_Get_Device_ID_ListW(L"USB", ids.data(), len, flags) != CR_SUCCESS) return false;
    for (auto& c : ids) c = towupper(c);
    return ids.find(L"VID_045E&PID_0B12") != std::wstring::npos;
}

DWORD WINAPI ReadThread(LPVOID) {
    HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    UINT64 address = g_address;
    // The controller's address type is not known from the advertisement here: try random, then public.
    int raw = ReadLeBatteryLevel(address, true, false);
    if (raw < 0) raw = ReadLeBatteryLevel(address, false, false);
    if (SUCCEEDED(init)) RoUninitialize();
    std::lock_guard<std::mutex> lock(g_state.m);
    if (raw >= 0) {
        bool charging = false;
        g_state.percent = g_state.trend.Add(raw, charging);
        g_state.charging = charging;
        g_state.lastOk = GetTickCount64();
    }
    g_state.running = false;
    return 0;
}

}  // namespace

void ArmorXOnAdvert(UINT64 addr) {
    if (addr) g_address = addr;
}

void PollArmorX(PollContext& ctx) {
    if (!DonglePresent()) return;  // no Bluetooth scan for everyone else
    BleWantActiveScan();
    EnsureBleWatcher();
    if (!g_address) return;
    // The dongle's "Full" is never real. 0B12 is also a real Xbox Series pad, so it is only hidden once
    // an Armor X has been heard.
    ctx.Claim(0x045E, 0x0B12);
    std::lock_guard<std::mutex> lock(g_state.m);
    ULONGLONG now = GetTickCount64();
    if (!g_state.running && (!g_state.lastAttempt || now - g_state.lastAttempt >= kEveryMs)) {
        g_state.lastAttempt = now;
        if (HANDLE t = CreateThread(nullptr, 0, ReadThread, nullptr, 0, nullptr)) {
            g_state.running = true;
            CloseHandle(t);
        }
    }
    if (!g_state.lastOk || now - g_state.lastOk >= kStaleMs) return;
    DeviceStatus s;
    s.key = L"armorx";  // one entry even if the controller's address changes
    s.name = L"Armor X Pro";
    s.via = L"Bluetooth";
    s.kind = DeviceKind::Gamepad;
    s.graceSec = 1800;  // its Bluetooth side is very weak (about -100 dBm), so reads drop out often
    s.percent = g_state.percent;
    s.charging = g_state.charging;
    ctx.out.push_back(s);
}
