// GameSir Cyclone Pro (T4cp) on its 2.4 GHz dongle (3537:1015).
// The dongle is a wired Xbox 360 pad to Windows (XInput "Full", no real level), but the pad keeps
// advertising over Bluetooth LE as "GameSir-Cyclone Pro" and answers GameSir's phone-app "base
// info" request there:
//   service 0x8650, characteristic 0x865F: enable notifications, write 01 01
//   the pad notifies a 19-byte packet whose last byte is the battery (gamesir_ble_protocol.h)
// 01 01 is a read-only "get base info" request (no setting is changed); notifications are switched
// back off afterwards. Service 0xFF10 on the same pad is the firmware-update channel: never touched.
// Protocol reference: github.com/bpbp-boop/GameSirBattery (worked out from GameSir's Android app,
// BluetoothLeService); tested on a real Cyclone Pro on its dongle.
//
// The pad has to be paired with Windows once (Settings > Bluetooth & devices > Add device); Windows
// then exposes each GATT service as a device interface whose class GUID is the service UUID, which
// the Win32 GATT API (bluetoothleapis.h) can open. Connecting can take seconds (or time out while
// the pad is off), so the query runs on its own thread at most every two minutes and the poll only
// reports the cached result. While the pad is known, the dongle is claimed so the XInput fallback
// does not also show it as a full "Xbox controller".
#include "drivers.h"

#include <cfgmgr32.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <bthledef.h>
#include <bluetoothleapis.h>

#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#include "gamesir_ble_protocol.h"

namespace {

const GUID kInfoService = { 0x00008650, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB } };
const USHORT kInfoChar = 0x865F;
const USHORT kVid = 0x3537;
const USHORT kDonglePid = 0x1015;

const ULONGLONG kQueryEveryMs = 120 * 1000;  // successful reads
const ULONGLONG kRetryMs = 60 * 1000;        // after a failed read (pad off, asleep or out of range)
const ULONGLONG kStaleMs = 6 * 60 * 1000;    // stop reporting a reading this old (grace then shows "asleep")

struct Pad {
    std::wstring path;     // GATT service 0x8650 interface
    std::wstring address;  // 12 hex digits, from the instance id
};

struct State {
    std::mutex m;
    bool running = false;
    ULONGLONG lastAttempt = 0;
    ULONGLONG lastOk = 0;
    GameSirBle::Reading reading;
    std::wstring address;
    std::wstring path;
};
State g_state;

std::wstring StrProp(HDEVINFO di, SP_DEVINFO_DATA* dd, const DEVPROPKEY& key) {
    wchar_t buf[256];
    DEVPROPTYPE t;
    if (SetupDiGetDevicePropertyW(di, dd, &key, &t, (PBYTE)buf, sizeof(buf), nullptr, 0) && t == DEVPROP_TYPE_STRING) return buf;
    return L"";
}

// Name of the BTHLE\DEV_<address> node the service belongs to.
std::wstring ParentName(DEVINST child) {
    DEVINST parent;
    wchar_t id[MAX_DEVICE_ID_LEN];
    if (CM_Get_Parent(&parent, child, 0) != CR_SUCCESS || CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return L"";
    HDEVINFO di = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (di == INVALID_HANDLE_VALUE) return L"";
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    std::wstring name;
    if (SetupDiOpenDeviceInfoW(di, id, nullptr, 0, &dd)) {
        name = StrProp(di, &dd, DEVPKEY_Device_FriendlyName);
        if (name.empty()) name = StrProp(di, &dd, DEVPKEY_Device_DeviceDesc);
    }
    SetupDiDestroyDeviceInfoList(di);
    return name;
}

// Paired Cyclone pads exposing the base-info service.
std::vector<Pad> FindPads() {
    std::vector<Pad> pads;
    HDEVINFO di = SetupDiGetClassDevsW(&kInfoService, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return pads;
    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, nullptr, &kInfoService, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> buf(need);
        auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA dd{ sizeof(dd) };
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, detail, need, nullptr, &dd)) continue;
        std::wstring name = ParentName(dd.DevInst);
        if (_wcsnicmp(name.c_str(), L"GameSir-Cyclone", 15) != 0) continue;
        Pad p;
        p.path = detail->DevicePath;
        // BTHLEDEVICE\{service}_Dev_VID&...&PID&..._<address>\...: the address follows the last '_'.
        wchar_t inst[512];
        if (SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) {
            std::wstring s = inst;
            size_t slash = s.find(L'\\', s.find(L'\\') + 1);
            std::wstring mid = s.substr(0, slash);
            size_t us = mid.rfind(L'_');
            if (us != std::wstring::npos && mid.size() - us - 1 == 12) p.address = mid.substr(us + 1);
        }
        pads.push_back(p);
    }
    SetupDiDestroyDeviceInfoList(di);
    return pads;
}

bool IsChar(const BTH_LE_GATT_CHARACTERISTIC& c, USHORT shortUuid) {
    if (c.CharacteristicUuid.IsShortUuid) return c.CharacteristicUuid.Value.ShortUuid == shortUuid;
    const GUID& g = c.CharacteristicUuid.Value.LongUuid;
    GUID want = kInfoService;
    want.Data1 = shortUuid;
    return IsEqualGUID(g, want);
}

struct Wait {
    HANDLE done;
    GameSirBle::Reading reading;
};

void CALLBACK OnValue(BTH_LE_GATT_EVENT_TYPE type, PVOID param, PVOID ctx) {
    if (type != CharacteristicValueChangedEvent || !param || !ctx) return;
    auto* ev = (BLUETOOTH_GATT_VALUE_CHANGED_EVENT*)param;
    auto* w = (Wait*)ctx;
    if (!ev->CharacteristicValue) return;
    GameSirBle::Reading r;
    if (GameSirBle::Parse(ev->CharacteristicValue->Data, ev->CharacteristicValue->DataSize, r)) {
        w->reading = r;
        SetEvent(w->done);
    }
}

// One base-info round trip. Blocking: run off the poll thread.
bool Query(const std::wstring& path, GameSirBle::Reading& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    BTH_LE_GATT_SERVICE service{};
    USHORT n = 0;
    std::vector<BTH_LE_GATT_CHARACTERISTIC> chars;
    BTH_LE_GATT_CHARACTERISTIC* info = nullptr;
    PBTH_LE_GATT_DESCRIPTOR cccd = nullptr;
    std::vector<BTH_LE_GATT_DESCRIPTOR> descs;
    BLUETOOTH_GATT_EVENT_HANDLE evt = nullptr;
    Wait w{ CreateEventW(nullptr, TRUE, FALSE, nullptr), {} };

    if (BluetoothGATTGetServices(h, 1, &service, &n, BLUETOOTH_GATT_FLAG_NONE) != S_OK || n == 0) goto done;
    BluetoothGATTGetCharacteristics(h, &service, 0, nullptr, &n, BLUETOOTH_GATT_FLAG_NONE);
    if (!n) goto done;
    chars.resize(n);
    if (BluetoothGATTGetCharacteristics(h, &service, n, chars.data(), &n, BLUETOOTH_GATT_FLAG_NONE) != S_OK) goto done;
    for (auto& c : chars)
        if (IsChar(c, kInfoChar)) info = &c;
    if (!info || !info->IsNotifiable) goto done;

    n = 0;
    BluetoothGATTGetDescriptors(h, info, 0, nullptr, &n, BLUETOOTH_GATT_FLAG_NONE);
    if (!n) goto done;
    descs.resize(n);
    if (BluetoothGATTGetDescriptors(h, info, n, descs.data(), &n, BLUETOOTH_GATT_FLAG_NONE) != S_OK) goto done;
    for (auto& d : descs)
        if (d.DescriptorType == ClientCharacteristicConfiguration) cccd = &d;
    if (!cccd || !w.done) goto done;

    {
        BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION reg{};
        reg.NumCharacteristics = 1;
        reg.Characteristics[0] = *info;
        if (BluetoothGATTRegisterEvent(h, CharacteristicValueChangedEvent, &reg, OnValue, &w, &evt, BLUETOOTH_GATT_FLAG_NONE) != S_OK) goto done;

        BTH_LE_GATT_DESCRIPTOR_VALUE on{};
        on.DescriptorType = ClientCharacteristicConfiguration;
        on.ClientCharacteristicConfiguration.IsSubscribeToNotification = TRUE;
        if (BluetoothGATTSetDescriptorValue(h, cccd, &on, BLUETOOTH_GATT_FLAG_NONE) != S_OK) goto done;

        BYTE buf[sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + 2] = {};
        auto* v = (PBTH_LE_GATT_CHARACTERISTIC_VALUE)buf;
        v->DataSize = 2;
        v->Data[0] = 0x01;
        v->Data[1] = 0x01;
        ULONG flags = info->IsWritableWithoutResponse ? BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE : BLUETOOTH_GATT_FLAG_NONE;
        if (BluetoothGATTSetCharacteristicValue(h, info, v, 0, flags) == S_OK && WaitForSingleObject(w.done, 3000) == WAIT_OBJECT_0) {
            out = w.reading;
            ok = true;
        }

        BTH_LE_GATT_DESCRIPTOR_VALUE off{};
        off.DescriptorType = ClientCharacteristicConfiguration;
        BluetoothGATTSetDescriptorValue(h, cccd, &off, BLUETOOTH_GATT_FLAG_NONE);
    }

done:
    if (evt) BluetoothGATTUnregisterEvent(evt, BLUETOOTH_GATT_FLAG_NONE);
    if (w.done) CloseHandle(w.done);
    CloseHandle(h);
    return ok;
}

DWORD WINAPI QueryThread(LPVOID) {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(g_state.m);
        path = g_state.path;
    }
    GameSirBle::Reading r;
    // The first GATT calls after the link comes up often fail; one retry covers it.
    bool ok = Query(path, r) || (Sleep(1500), Query(path, r));
    std::lock_guard<std::mutex> lock(g_state.m);
    if (ok) {
        // Charging reports no level: keep the last known percentage next to the bolt.
        if (r.charging && g_state.lastOk) r.percent = g_state.reading.percent;
        g_state.reading = r;
        g_state.lastOk = GetTickCount64();
    }
    g_state.running = false;
    return 0;
}

}  // namespace

void PollGameSirBle(PollContext& ctx) {
    std::vector<Pad> pads = FindPads();
    if (pads.empty()) return;
    const Pad& pad = pads.front();  // one Cyclone Pro per PC; a second pad would share the dongle id anyway

    bool dongle = false;
    for (const auto& c : ctx.colls) dongle |= c.vid == kVid && c.pid == kDonglePid;

    HANDLE first = nullptr;  // the very first read is waited for (app start, --list)
    {
        ULONGLONG now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_state.m);
        if (g_state.address != pad.address) {  // another pad paired: forget the old reading
            g_state.address = pad.address;
            g_state.lastOk = 0;
            g_state.lastAttempt = 0;
        }
        g_state.path = pad.path;
        ULONGLONG wait = g_state.lastOk && g_state.lastOk >= g_state.lastAttempt ? kQueryEveryMs : kRetryMs;
        if (!g_state.running && (!g_state.lastAttempt || now - g_state.lastAttempt >= wait)) {
            bool isFirst = !g_state.lastAttempt;
            g_state.lastAttempt = now;
            if (HANDLE t = CreateThread(nullptr, 0, QueryThread, nullptr, 0, nullptr)) {
                g_state.running = true;
                if (isFirst) first = t;
                else CloseHandle(t);
            }
        }
    }
    if (first) {
        // One absolute 4 s deadline, once per run, so the first refresh (and --list) can show the
        // level: an awake pad answers in about 2-3 s. Later polls never wait; a read that is still
        // running (pad asleep) finishes on its thread and shows on the next refresh.
        WaitForSingleObject(first, 4000);
        CloseHandle(first);
    }

    // The dongle's XInput battery is meaningless ("Full"), so keep it out of the fallbacks.
    if (dongle) ctx.Claim(kVid, kDonglePid);
    std::lock_guard<std::mutex> lock(g_state.m);
    if (!g_state.lastOk || GetTickCount64() - g_state.lastOk >= kStaleMs) return;

    DeviceStatus s;
    s.key = L"gamesir-ble:" + pad.address;
    s.name = L"GameSir Cyclone Pro";
    s.kind = DeviceKind::Gamepad;
    s.via = dongle ? L"2.4 GHz receiver (battery over Bluetooth LE)" : L"Bluetooth LE";
    s.percent = g_state.reading.percent;
    s.charging = g_state.reading.charging;
    s.graceSec = 600;
    s.vid = kVid;
    ctx.Add(s, kVid, dongle ? kDonglePid : 0);
}
