// Shared BLE advertisement watcher: AirPods proximity messages and Fast Pair battery broadcasts.
// See ble_adverts.h.
#include "ble_adverts.h"

#include <eventtoken.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

#include <string>

#include <cwchar>

// ---- minimal WinRT ABI declarations (MinGW ships no windows.devices.bluetooth.advertisement.h) ----
// Vtable order from the Windows SDK header windows.devices.bluetooth.advertisement.h.
// Deliberately NOT in an anonymous namespace: GCC would see interfaces nobody in the program can
// implement and turn every call through them into a trap (-fdevirtualize, at -O1 and above).
namespace ble_abi {

const GUID IID_Watcher = { 0xa6ac336f, 0xf3d3, 0x4297, { 0x8d, 0x6c, 0xc8, 0x1e, 0xa6, 0x62, 0x3f, 0x40 } };
const GUID IID_ReceivedArgs = { 0x27987ddf, 0xe596, 0x41be, { 0x8d, 0x43, 0x9e, 0x67, 0x31, 0xd4, 0xa9, 0x13 } };
// ITypedEventHandler<BluetoothLEAdvertisementWatcher, BluetoothLEAdvertisementReceivedEventArgs>
const GUID IID_ReceivedHandler = { 0x90eb4eca, 0xd465, 0x5ea0, { 0xa6, 0x1c, 0x03, 0x3c, 0x8c, 0x5e, 0xce, 0xf2 } };
const GUID IID_BufferByteAccess = { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };
const GUID IID_Unknown = { 0x00000000, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
const GUID IID_Agile = { 0x94ea2b94, 0xe9cc, 0x49e0, { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };

struct IBufferAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Length(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Length(UINT32 v) = 0;
};

struct IBufferByteAccessAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE** v) = 0;
};

struct IManufacturerData : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_CompanyId(UINT16* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_CompanyId(UINT16 v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Data(IBufferAbi** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Data(IBufferAbi* v) = 0;
};

struct IManufacturerDataView : IInspectable {  // IVectorView<BluetoothLEManufacturerData>
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 i, IManufacturerData** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) = 0;
};

struct IDataSection : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DataType(BYTE* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DataType(BYTE v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Data(IBufferAbi** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Data(IBufferAbi* v) = 0;
};

struct IDataSectionView : IInspectable {  // IVectorView<BluetoothLEAdvertisementDataSection>
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 i, IDataSection** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) = 0;
};

struct IAdvertisement : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Flags(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Flags(IInspectable* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LocalName(HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_LocalName(HSTRING v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ServiceUuids(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ManufacturerData(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DataSections(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetManufacturerDataByCompanyId(UINT16 company, IManufacturerDataView** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetSectionsByType(BYTE type, IDataSectionView** v) = 0;
};

struct IReceivedArgs : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_RawSignalStrengthInDBm(INT16* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BluetoothAddress(UINT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AdvertisementType(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Timestamp(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Advertisement(IAdvertisement** v) = 0;
};

struct IWatcher : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinSamplingInterval(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxSamplingInterval(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MinOutOfRangeTimeout(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MaxOutOfRangeTimeout(INT64* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ScanningMode(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ScanningMode(INT32 v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SignalStrengthFilter(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_SignalStrengthFilter(IInspectable* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AdvertisementFilter(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AdvertisementFilter(IInspectable* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE Start() = 0;
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Received(IUnknown* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Received(EventRegistrationToken token) = 0;
};

}  // namespace ble_abi

namespace {

using namespace ble_abi;

enum { kWatcherStopped = 3, kWatcherAborted = 4 };
constexpr BYTE kServiceData16 = 0x16;  // AD type "Service Data - 16-bit UUID"

template <class T>
void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// Raw bytes of a WinRT IBuffer (valid while `buf` is alive).
bool Bytes(IBufferAbi* buf, BYTE*& p, UINT32& len) {
    IBufferByteAccessAbi* access = nullptr;
    p = nullptr;
    len = 0;
    bool ok = buf && SUCCEEDED(buf->get_Length(&len)) && SUCCEEDED(buf->QueryInterface(IID_BufferByteAccess, (void**)&access)) &&
              access && SUCCEEDED(access->Buffer(&p)) && p;
    Release(access);
    return ok;
}

void OnReceived(IReceivedArgs* args) {
    IAdvertisement* adv = nullptr;
    if (FAILED(args->get_Advertisement(&adv)) || !adv) return;
    UINT64 addr = 0;
    INT16 rssi = -127;
    args->get_BluetoothAddress(&addr);
    args->get_RawSignalStrengthInDBm(&rssi);

    // The BigBigWon Armor X Pro advertises its name ("ARMOR-X...") in the scan response (active scan, PR #145).
    HSTRING local = nullptr;
    if (SUCCEEDED(adv->get_LocalName(&local)) && local) {
        UINT32 n = 0;
        const wchar_t* s = WindowsGetStringRawBuffer(local, &n);
        if (s && n && std::wstring(s, n).find(L"ARMOR-X") != std::wstring::npos) ArmorXOnAdvert(addr);
        WindowsDeleteString(local);
    }

    IManufacturerDataView* mview = nullptr;
    if (SUCCEEDED(adv->GetManufacturerDataByCompanyId(0x004C, &mview)) && mview) {
        UINT32 count = 0;
        mview->get_Size(&count);
        for (UINT32 i = 0; i < count; i++) {
            IManufacturerData* md = nullptr;
            IBufferAbi* buf = nullptr;
            BYTE* p;
            UINT32 len;
            if (SUCCEEDED(mview->GetAt(i, &md)) && md && SUCCEEDED(md->get_Data(&buf)) && Bytes(buf, p, len))
                AirPodsOnAdvert(addr, rssi, p, len);
            Release(buf);
            Release(md);
        }
    }
    Release(mview);

    IDataSectionView* sview = nullptr;
    if (SUCCEEDED(adv->GetSectionsByType(kServiceData16, &sview)) && sview) {
        UINT32 count = 0;
        sview->get_Size(&count);
        for (UINT32 i = 0; i < count; i++) {
            IDataSection* sec = nullptr;
            IBufferAbi* buf = nullptr;
            BYTE* p;
            UINT32 len;
            // Service data starts with the 16-bit UUID, little-endian: 2C FE = Google Fast Pair.
            if (SUCCEEDED(sview->GetAt(i, &sec)) && sec && SUCCEEDED(sec->get_Data(&buf)) && Bytes(buf, p, len) && len > 2 &&
                p[0] == 0x2C && p[1] == 0xFE)
                FastPairOnAdvert(addr, p + 2, len - 2);
            Release(buf);
            Release(sec);
        }
    }
    Release(sview);
    Release(adv);
}

// Static delegate object: lives as long as the process.
struct ReceivedHandler : IUnknown {
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == IID_Unknown || iid == IID_ReceivedHandler || iid == IID_Agile) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable* /*sender*/, IInspectable* argsObj) {
        IReceivedArgs* args = nullptr;
        if (argsObj && SUCCEEDED(argsObj->QueryInterface(IID_ReceivedArgs, (void**)&args)) && args) {
            OnReceived(args);
            args->Release();
        }
        return S_OK;
    }
};

ReceivedHandler g_handler;
IWatcher* g_watcher = nullptr;
bool g_activeWanted = false;  // a driver needs names from scan responses (BleWantActiveScan)
bool g_activeSet = false;     // the watcher is in active mode

}  // namespace

bool EnsureBleWatcher() {
    if (!g_watcher) {
        RoInitialize(RO_INIT_MULTITHREADED);  // our poll thread; harmless if already initialised
        const wchar_t* cls = L"Windows.Devices.Bluetooth.Advertisement.BluetoothLEAdvertisementWatcher";
        HSTRING name = nullptr;
        IInspectable* obj = nullptr;
        if (SUCCEEDED(WindowsCreateString(cls, (UINT32)wcslen(cls), &name))) {
            RoActivateInstance(name, &obj);
            WindowsDeleteString(name);
        }
        if (!obj) return false;
        obj->QueryInterface(IID_Watcher, (void**)&g_watcher);
        obj->Release();
        if (!g_watcher) return false;
        EventRegistrationToken tok;
        if (FAILED(g_watcher->add_Received(&g_handler, &tok))) {
            Release(g_watcher);
            return false;
        }
        if (g_activeWanted && SUCCEEDED(g_watcher->put_ScanningMode(1))) g_activeSet = true;  // 1 = Active
        return SUCCEEDED(g_watcher->Start());
    }
    INT32 st = 0;
    if (FAILED(g_watcher->get_Status(&st))) return false;
    if (g_activeWanted && !g_activeSet) {
        // The mode can only change while stopped: stop now, switch and restart on a later call.
        if (st != kWatcherStopped && st != kWatcherAborted) {
            g_watcher->Stop();
            return false;
        }
        if (SUCCEEDED(g_watcher->put_ScanningMode(1))) g_activeSet = true;
    }
    if (st == kWatcherStopped || st == kWatcherAborted) return SUCCEEDED(g_watcher->Start());
    return false;
}

void BleWantActiveScan() { g_activeWanted = true; }
