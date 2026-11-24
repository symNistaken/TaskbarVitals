// Logitech G435 on its Jieli LIGHTSPEED receiver (4C4A:4155), from PR #632. This receiver exposes no
// readable battery level (its descriptors and the vendor SDK findings are in PR #632's
// docs/logitech_g435.md), so the level shown is an estimate, marked "~":
//  * Reference: Windows stores the paired G435's Bluetooth Hands-Free battery level in
//    DEVPKEY_Device_BatteryLevel on its BTHENUM nodes. Each new value calibrates the estimate (g435_estimate.h).
//  * Use: the receiver stays enumerated while the headset is off, so the headset only counts as connected
//    while one of the G435's USB audio endpoints (not the Bluetooth ones) has an active session or signal.
// While the headset is connected over Bluetooth this driver shows nothing: the Bluetooth driver reports
// the real level. Nothing is sent to the receiver or the headset. State is kept in
// HKCU\Software\WirelessBattery\G435.
#include "drivers.h"
#include "g435_estimate.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#include <ctime>
#include <map>
#include <cwctype>

namespace {

// IAudioMeterInformation (endpointvolume.h); some MinGW versions only forward-declare it.
struct IAudioMeter : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* peak) = 0;
};
const IID kIidAudioMeter = { 0xC02216F6, 0x8C67, 0x4B5B, { 0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64 } };

constexpr USHORT kVid = 0x4C4A, kPid = 0x4155;
const wchar_t* kRegKey = L"Software\\WirelessBattery\\G435";

// DEVPKEY_Device_BatteryLevel {104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2 (DEVPROP_TYPE_BYTE, 0..100)
const DEVPROPKEY kBatteryLevel = { { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } }, 2 };
// DEVPKEY_Device_IsConnected {83DA6326-97A6-4088-9453-A1923F573B29}, 15 (DEVPROP_TYPE_BOOLEAN)
const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };
// PKEY_Device_FriendlyName {A45C254E-DF1C-4EFD-8020-67D146A850E0}, 14
const PROPERTYKEY kFriendlyName = { { 0xA45C254E, 0xDF1C, 0x4EFD, { 0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0 } }, 14 };

std::wstring Lower(std::wstring s) {
    for (auto& ch : s) ch = (wchar_t)std::towlower(ch);
    return s;
}

// "g435" as a whole word, so no other device is taken for the headset.
bool IsG435Name(const std::wstring& name) {
    std::wstring low = Lower(name);
    for (size_t pos = 0; (pos = low.find(L"g435", pos)) != std::wstring::npos; pos += 4) {
        bool left = pos == 0 || !std::iswalnum(low[pos - 1]);
        bool right = pos + 4 >= low.size() || !std::iswalnum(low[pos + 4]);
        if (left && right) return true;
    }
    return false;
}

g435::State Load() {
    g435::State st;
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"LastReal", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS && v <= 100)
        st.lastReal = (int)v;
    sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"UsageSec", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        st.usageSec = v;
    sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"RateMilli", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS &&
        v >= (DWORD)g435::kMinRateMilli && v <= (DWORD)g435::kMaxRateMilli)
        st.rateMilli = (int)v;
    return st;
}

void Save(const g435::State& st) {
    DWORD real = st.lastReal < 0 ? 0xFFFFFFFF : (DWORD)st.lastReal;
    DWORD usage = (DWORD)std::min<std::uint64_t>(st.usageSec, 0xFFFFFFFF);
    DWORD rate = (DWORD)st.rateMilli;
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"LastReal", REG_DWORD, &real, sizeof(real));
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"UsageSec", REG_DWORD, &usage, sizeof(usage));
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegKey, L"RateMilli", REG_DWORD, &rate, sizeof(rate));
}

struct Bluetooth {
    bool connected = false;
    int percent = -1;
};

// The paired G435's Bluetooth state, from the BTHENUM nodes of its device container.
Bluetooth QueryBluetooth() {
    Bluetooth out;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;
    struct Container { bool g435 = false, connected = false; int hfp = -1, other = -1; };
    std::map<std::wstring, Container> containers;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        DEVPROPTYPE t = 0;
        GUID guid{};
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &t, (PBYTE)&guid, sizeof(guid), nullptr, 0) ||
            t != DEVPROP_TYPE_GUID)
            continue;
        wchar_t cid[64] = {};
        StringFromGUID2(guid, cid, 64);
        Container& c = containers[cid];
        wchar_t name[256] = {};
        if (SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_FriendlyName, &t, (PBYTE)name, sizeof(name) - sizeof(wchar_t), nullptr, 0) &&
            t == DEVPROP_TYPE_STRING && IsG435Name(name))
            c.g435 = true;
        std::wstring low = Lower(inst);
        if (low.rfind(L"bthenum\\dev_", 0) == 0) {
            DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
            if (SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) &&
                t == DEVPROP_TYPE_BOOLEAN)
                c.connected = conn != DEVPROP_FALSE;
        }
        BYTE pct = 0xFF;
        if (SetupDiGetDevicePropertyW(di, &dd, &kBatteryLevel, &t, &pct, sizeof(pct), nullptr, 0) &&
            t == DEVPROP_TYPE_BYTE && pct <= 100) {
            // The Hands-Free Audio Gateway node ({0000111E-...}) carries the headset's own report.
            if (low.find(L"{0000111e-0000-1000-8000-00805f9b34fb}") != std::wstring::npos) c.hfp = pct;
            else if (c.other < 0) c.other = pct;
        }
    }
    SetupDiDestroyDeviceInfoList(di);
    for (const auto& [cid, c] : containers) {
        if (!c.g435) continue;
        Bluetooth b;
        b.connected = c.connected;
        b.percent = c.hfp >= 0 ? c.hfp : c.other;
        if (b.connected || out.percent < 0) out = b;
        if (b.connected) break;
    }
    return out;
}

// True while one of the G435's USB audio endpoints is playing or recording.
bool UsbAudioInUse() {
    bool inUse = false;
    HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&en))) && en) {
        IMMDeviceCollection* list = nullptr;
        if (SUCCEEDED(en->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &list)) && list) {
            UINT count = 0;
            list->GetCount(&count);
            for (UINT i = 0; i < count && !inUse; i++) {
                IMMDevice* dev = nullptr;
                if (FAILED(list->Item(i, &dev)) || !dev) continue;
                std::wstring name;
                IPropertyStore* props = nullptr;
                if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    if (SUCCEEDED(props->GetValue(kFriendlyName, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal) name = pv.pwszVal;
                    PropVariantClear(&pv);
                    props->Release();
                }
                std::wstring low = Lower(name);
                bool bluetooth = low.find(L"hands-free") != std::wstring::npos || low.find(L"bluetooth") != std::wstring::npos;
                if (IsG435Name(name) && !bluetooth) {
                    IAudioMeter* meter = nullptr;
                    if (SUCCEEDED(dev->Activate(kIidAudioMeter, CLSCTX_INPROC_SERVER, nullptr,
                                                reinterpret_cast<void**>(&meter))) && meter) {
                        float peak = 0;
                        if (SUCCEEDED(meter->GetPeakValue(&peak)) && peak > 0.0001f) inUse = true;
                        meter->Release();
                    }
                    IAudioSessionManager2* mgr = nullptr;
                    if (!inUse && SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr,
                                                          reinterpret_cast<void**>(&mgr))) && mgr) {
                        IAudioSessionEnumerator* sessions = nullptr;
                        if (SUCCEEDED(mgr->GetSessionEnumerator(&sessions)) && sessions) {
                            int n = 0;
                            sessions->GetCount(&n);
                            for (int k = 0; k < n && !inUse; k++) {
                                IAudioSessionControl* ctl = nullptr;
                                if (SUCCEEDED(sessions->GetSession(k, &ctl)) && ctl) {
                                    AudioSessionState state = AudioSessionStateInactive;
                                    if (SUCCEEDED(ctl->GetState(&state)) && state == AudioSessionStateActive) inUse = true;
                                    ctl->Release();
                                }
                            }
                            sessions->Release();
                        }
                        mgr->Release();
                    }
                }
                dev->Release();
            }
            list->Release();
        }
        en->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return inUse;
}

}  // namespace

void PollLogitechG435Jieli(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    bool present = false;
    for (const HidColl& c : ctx.colls) present |= c.vid == kVid && c.pid == kPid;
    if (!present) return;
    ctx.Claim(kVid, kPid);

    g435::State st = Load();
    bool dirty = false;
    Bluetooth bt = QueryBluetooth();
    if (bt.percent >= 0) dirty |= g435::RecordReading(st, bt.percent);
    const std::uint64_t now = (std::uint64_t)std::time(nullptr);
    bool inUse = !bt.connected && UsbAudioInUse();
    dirty |= g435::Accumulate(st, inUse, now);
    if (dirty) Save(st);
    if (bt.connected || !inUse) return;  // on Bluetooth (reported there), or off / idle behind the receiver

    DeviceStatus s;
    s.key = L"g435:jieli";
    s.name = L"Logitech G435";
    s.kind = DeviceKind::Headset;
    s.via = L"LIGHTSPEED";
    s.vid = 0x046D;
    int pct = g435::Estimate(st);
    if (pct >= 0) {
        // An estimate, not a reading: shown as "~XX%" and kept out of the history and low-battery alerts.
        s.levelText = L"~" + std::to_wstring(pct) + L"%";
        s.detail = st.rateMilli ? L"Estimated from use since the last Bluetooth reading (" + std::to_wstring(st.lastReal) + L"%)"
                                : L"Last Bluetooth reading";
    } else {
        s.note = L"Connect the headset over Bluetooth once to calibrate the estimate";
    }
    ctx.out.push_back(s);
}
