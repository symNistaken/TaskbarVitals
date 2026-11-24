// Marshall EMBERTON II Bluetooth speaker.
//
// Paired with Windows the speaker is audio only (A2DP sink + AVRCP, no Hands-Free), so Windows has no
// battery level for it and the Bluetooth fallback skips it. The level lives on its Bluetooth LE side,
// which Marshall's app reads: the standard Battery Service 0x180F, Battery Level 0x2A19, one byte
// 0-100 (10% steps). Windows does not pair that LE side (pairing fails), but the speaker lets an
// unpaired client read Battery Level.
//
// The LE side advertises from a rotating private address, so the advertisement alone does not say
// which "EMBERTON II [LE]" is the one paired with this PC. The speaker's Fast Pair Message Stream
// (RFCOMM, reachable through the existing Classic pairing) does: right after the stream opens the
// speaker sends its Fast Pair model id and its current LE address (fastpair_stream.h). Nothing is
// ever sent on the stream, and on LE only Battery Level is read (no writes, no notifications).
//
// Windows only opens a random LE address it has recently heard advertising, so the shared passive
// advertisement watcher (ble_adverts.cpp, listen only) is started and the lookup is retried for a few
// seconds; the speaker advertises about once a second.
//
// Right after an LE connection the speaker can report a placeholder 100% for about two seconds before
// the real level, so the level is read twice, 2.5 s apart, and the second read is used.
//
// Worked out from an Android HCI log of the Marshall Bluetooth app (Battery Level matched the app at
// 50% and 40%) and nRF Connect, then tested from Windows on a real EMBERTON II (firmware 14.0.35).
//
// Connecting takes seconds (or fails while the speaker is out of range), so the query runs on its own
// thread at most every two minutes and the poll reports the cached result, like gamesir_ble.cpp.
#include <winsock2.h>
#include <ws2bth.h>

#include "drivers.h"
#include "ble_adverts.h"
#include "fastpair_stream.h"

#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <inspectable.h>
#include <roapi.h>
#include <winstring.h>

#include <cwchar>
#include <cwctype>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// ---- minimal WinRT ABI declarations (MinGW's headers lack IBluetoothLEDevice3) ----
// Interface ids and vtable order checked against Windows' own metadata (Windows.Devices.winmd) and the
// MinGW-w64 windows.devices.bluetooth*.h / windows.foundation*.h headers.
// Deliberately NOT in an anonymous namespace, for the reason given in ble_adverts.cpp (GCC would turn
// calls through interfaces nobody in the program implements into traps).
namespace marshall_abi {

// IBluetoothLEDeviceStatics2 {5F12C06B-3BAC-43E8-AD16-563271BD41C2}
const GUID IID_LEDeviceStatics2 = { 0x5f12c06b, 0x3bac, 0x43e8, { 0xad, 0x16, 0x56, 0x32, 0x71, 0xbd, 0x41, 0xc2 } };
// IBluetoothLEDevice3 {AEE9E493-44AC-40DC-AF33-B2C13C01CA46}
const GUID IID_LEDevice3 = { 0xaee9e493, 0x44ac, 0x40dc, { 0xaf, 0x33, 0xb2, 0xc1, 0x3c, 0x01, 0xca, 0x46 } };
// IGattDeviceService3 {B293A950-0C53-437C-A9B3-5C3210C6E569}
const GUID IID_GattDeviceService3 = { 0xb293a950, 0x0c53, 0x437c, { 0xa9, 0xb3, 0x5c, 0x32, 0x10, 0xc6, 0xe5, 0x69 } };
// IAsyncInfo {00000036-0000-0000-C000-000000000046}
const GUID IID_AsyncInfo = { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
// IClosable {30D5A829-7FA4-4026-83BB-D75BAE4EA99E}
const GUID IID_Closable = { 0x30d5a829, 0x7fa4, 0x4026, { 0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e } };
// IBufferByteAccess {905A0FEF-BC53-11DF-8C49-001E4FC686DA}
const GUID IID_BufferByteAccess = { 0x905a0fef, 0xbc53, 0x11df, { 0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda } };

enum { kAsyncStarted = 0, kAsyncCompleted = 1 };
enum { kCacheModeUncached = 1 };
enum { kAddressTypeRandom = 1 };
enum { kGattSuccess = 0 };

struct IAsyncInfoAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

// IAsyncOperation<T> where T is a runtime class: GetResults returns T's default interface.
struct IAsyncOp : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IInspectable** v) = 0;
};

// IVectorView<T> where T is a runtime class (only the first two methods are used).
struct IVectorViewAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 i, IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* v) = 0;
};

struct ILEDeviceStatics2 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelectorFromPairingState(BYTE paired, HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelectorFromConnectionStatus(INT32 status, HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelectorFromDeviceName(HSTRING name, HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelectorFromBluetoothAddress(UINT64 addr, HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelectorFromBluetoothAddressWithType(UINT64 addr, INT32 type, HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelectorFromAppearance(IInspectable* appearance, HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE FromBluetoothAddressWithTypeAsync(UINT64 addr, INT32 type, IAsyncOp** op) = 0;
};

struct ILEDevice3 : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DeviceAccessInformation(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesAsync(IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesWithCacheModeAsync(INT32 mode, IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesForUuidAsync(GUID uuid, IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetGattServicesForUuidWithCacheModeAsync(GUID uuid, INT32 mode, IAsyncOp** op) = 0;
};

// IGattDeviceServicesResult and IGattCharacteristicsResult: status, protocol error, items.
struct IGattListResult : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ProtocolError(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Items(IVectorViewAbi** v) = 0;
};

struct IGattDeviceService3Abi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DeviceAccessInformation(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Session(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SharingMode(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE OpenAsync(INT32 mode, IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharacteristicsAsync(IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharacteristicsWithCacheModeAsync(INT32 mode, IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharacteristicsForUuidAsync(GUID uuid, IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCharacteristicsForUuidWithCacheModeAsync(GUID uuid, INT32 mode, IAsyncOp** op) = 0;
};

struct IGattCharacteristicAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetDescriptors(GUID uuid, IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_CharacteristicProperties(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ProtectionLevel(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ProtectionLevel(INT32 v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UserDescription(HSTRING* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Uuid(GUID* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AttributeHandle(UINT16* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PresentationFormats(IInspectable** v) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadValueAsync(IAsyncOp** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE ReadValueWithCacheModeAsync(INT32 mode, IAsyncOp** op) = 0;
};

struct IBufferAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Capacity(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Length(UINT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Length(UINT32 v) = 0;
};

struct IBufferByteAccessAbi : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Buffer(BYTE** v) = 0;
};

struct IGattReadResultAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32* v) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Value(IBufferAbi** v) = 0;
};

struct IClosableAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

}  // namespace marshall_abi

namespace {

using namespace marshall_abi;

// {df21fe2c-2515-4fdb-8886-f12c4d67927c}: Fast Pair Message Stream (RFCOMM).
const GUID kMessageStream = { 0xdf21fe2c, 0x2515, 0x4fdb, { 0x88, 0x86, 0xf1, 0x2c, 0x4d, 0x67, 0x92, 0x7c } };
const wchar_t kStreamNodePrefix[] = L"BTHENUM\\{DF21FE2C-2515-4FDB-8886-F12C4D67927C}";
const GUID kBatteryService = { 0x0000180f, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
const GUID kBatteryLevel = { 0x00002a19, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };

// Fast Pair model ids this driver reads. Only the EMBERTON II has been checked; other Marshall
// speakers may work the same way but are not matched until someone verifies them.
constexpr std::uint32_t kModelIds[] = { 0x15AE46 };  // EMBERTON II

const ULONGLONG kQueryEveryMs = 120 * 1000;  // successful reads
const ULONGLONG kRetryMs = 60 * 1000;        // after a failed read (speaker off, out of range)
const ULONGLONG kStaleMs = 6 * 60 * 1000;    // stop reporting a reading this old
const DWORD kSettleMs = 2500;                // between the two reads (placeholder 100% after connecting)
const int kLookupTries = 10;                 // 500 ms apart: wait up to ~5 s to hear the LE address

struct Speaker {
    std::wstring name;
    std::wstring addrText;  // 12 hex digits
    ULONGLONG addr = 0;     // Classic (public) address
};

struct Entry {
    ULONGLONG lastAttempt = 0;
    ULONGLONG lastOk = 0;
    int percent = -1;
};

struct State {
    std::mutex m;
    bool running = false;
    std::map<ULONGLONG, Entry> entries;  // by Classic address
    std::vector<Speaker> pending;        // handed to the query thread
};
State g_state;

template <class T>
void Release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

bool LooksLikeEmberton(const std::wstring& name) {
    std::wstring n = name;
    for (auto& ch : n) ch = towlower(ch);
    return n.rfind(L"emberton", 0) == 0;
}

// Paired, connected EMBERTON speakers whose Classic pairing exposes the Fast Pair Message Stream.
std::vector<Speaker> FindConnectedSpeakers() {
    std::vector<Speaker> out;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;
    std::vector<std::wstring> streamNodes;
    std::vector<Speaker> candidates;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512];
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        if (_wcsnicmp(inst, kStreamNodePrefix, wcslen(kStreamNodePrefix)) == 0) {
            streamNodes.push_back(inst);
            continue;
        }
        if (_wcsnicmp(inst, L"BTHENUM\\DEV_", 12) != 0) continue;
        std::wstring hex(inst + 12, wcsnlen(inst + 12, 12));
        if (hex.size() != 12) continue;
        wchar_t* end = nullptr;
        ULONGLONG addr = wcstoull(hex.c_str(), &end, 16);
        if (!addr || *end) continue;

        DEVPROPTYPE t;
        DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
        if (!SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) ||
            t != DEVPROP_TYPE_BOOLEAN || conn == DEVPROP_FALSE)
            continue;
        wchar_t name[256] = {};
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_FriendlyName, &t, (PBYTE)name, sizeof(name) - sizeof(wchar_t),
                                       nullptr, 0) ||
            t != DEVPROP_TYPE_STRING)
            continue;
        if (!LooksLikeEmberton(name)) continue;
        for (auto& ch : hex) ch = towupper(ch);
        candidates.push_back({ name, hex, addr });
    }
    SetupDiDestroyDeviceInfoList(di);
    // Service node ids end in "...&<address>_C00000000".
    for (const Speaker& s : candidates)
        for (const std::wstring& node : streamNodes)
            if (node.find(L"&" + s.addrText + L"_") != std::wstring::npos) {
                out.push_back(s);
                break;
            }
    return out;
}

bool StartWinsock() {
    static bool ok = [] {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    return ok;
}

bool WaitSocket(SOCKET s, bool forWrite, int timeoutMs) {
    fd_set set, err;
    FD_ZERO(&set);
    FD_ZERO(&err);
    FD_SET(s, &set);
    FD_SET(s, &err);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    int r = select(0, forWrite ? nullptr : &set, forWrite ? &set : nullptr, &err, &tv);
    return r > 0 && FD_ISSET(s, &set) && !FD_ISSET(s, &err);
}

// Opens the Message Stream and collects what the speaker sends by itself. Listen only.
bool ReadStreamInfo(ULONGLONG addr, FastPairStream::Info& info) {
    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    SOCKADDR_BTH sa{};
    sa.addressFamily = AF_BTH;
    sa.btAddr = addr;
    sa.serviceClassId = kMessageStream;
    sa.port = 0;
    if (connect(s, (sockaddr*)&sa, sizeof(sa)) == 0 || (WSAGetLastError() == WSAEWOULDBLOCK && WaitSocket(s, true, 3000))) {
        std::vector<std::uint8_t> buf;
        ULONGLONG deadline = GetTickCount64() + 2000;
        while (!(info.hasModelId && info.hasLeAddress) && buf.size() < 4096) {
            ULONGLONG now = GetTickCount64();
            if (now >= deadline || !WaitSocket(s, false, (int)(deadline - now))) break;
            char chunk[512];
            int n = recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            buf.insert(buf.end(), chunk, chunk + n);
            size_t used = FastPairStream::Parse(buf.data(), buf.size(), info);
            buf.erase(buf.begin(), buf.begin() + used);
        }
    }
    closesocket(s);
    return info.hasModelId && info.hasLeAddress;
}

bool KnownModel(std::uint32_t id) {
    for (std::uint32_t m : kModelIds)
        if (m == id) return true;
    return false;
}

// Waits for a WinRT async operation (polling, no completion handler) and returns its result, or null.
// Releases `op`.
IInspectable* Await(IAsyncOp* op, DWORD timeoutMs) {
    if (!op) return nullptr;
    IAsyncInfoAbi* info = nullptr;
    IInspectable* result = nullptr;
    if (SUCCEEDED(op->QueryInterface(IID_AsyncInfo, (void**)&info)) && info) {
        ULONGLONG deadline = GetTickCount64() + timeoutMs;
        INT32 st = kAsyncStarted;
        while (SUCCEEDED(info->get_Status(&st)) && st == kAsyncStarted && GetTickCount64() < deadline) Sleep(20);
        if (st == kAsyncCompleted) op->GetResults(&result);
        else if (st == kAsyncStarted) info->Cancel();
        Release(info);
    }
    op->Release();
    return result;
}

void CloseAndRelease(IInspectable*& p) {
    if (!p) return;
    IClosableAbi* c = nullptr;
    if (SUCCEEDED(p->QueryInterface(IID_Closable, (void**)&c)) && c) {
        c->Close();
        c->Release();
    }
    Release(p);
}

// One Battery Level read, uncached (goes to the speaker). -1 on failure.
int ReadLevelOnce(IGattCharacteristicAbi* ch) {
    IAsyncOp* op = nullptr;
    if (FAILED(ch->ReadValueWithCacheModeAsync(kCacheModeUncached, &op))) return -1;
    IInspectable* obj = Await(op, 8000);
    IGattReadResultAbi* res = (IGattReadResultAbi*)obj;  // default interface of GattReadResult
    int level = -1;
    INT32 st = -1;
    IBufferAbi* buf = nullptr;
    IBufferByteAccessAbi* access = nullptr;
    BYTE* p = nullptr;
    UINT32 len = 0;
    if (res && SUCCEEDED(res->get_Status(&st)) && st == kGattSuccess && SUCCEEDED(res->get_Value(&buf)) && buf &&
        SUCCEEDED(buf->get_Length(&len)) && len >= 1 && SUCCEEDED(buf->QueryInterface(IID_BufferByteAccess, (void**)&access)) &&
        access && SUCCEEDED(access->Buffer(&p)) && p && p[0] <= 100)
        level = p[0];
    Release(access);
    Release(buf);
    Release(obj);
    return level;
}

// First item of a GATT services / characteristics result, or null.
IInspectable* FirstItem(IInspectable* resultObj) {
    IGattListResult* res = (IGattListResult*)resultObj;
    IVectorViewAbi* items = nullptr;
    IInspectable* first = nullptr;
    INT32 st = -1;
    UINT32 n = 0;
    if (res && SUCCEEDED(res->get_Status(&st)) && st == kGattSuccess && SUCCEEDED(res->get_Items(&items)) && items &&
        SUCCEEDED(items->get_Size(&n)) && n > 0)
        items->GetAt(0, &first);
    Release(items);
    return first;
}

// Battery Level of the speaker's LE side at its current random address, without pairing. -1 on failure.
// Blocking (several seconds): run off the poll thread.
int ReadBatteryLevel(ULONGLONG leAddress, int addressType = kAddressTypeRandom, bool settle = true) {
    const wchar_t* cls = L"Windows.Devices.Bluetooth.BluetoothLEDevice";
    HSTRING name = nullptr;
    ILEDeviceStatics2* statics = nullptr;
    if (SUCCEEDED(WindowsCreateString(cls, (UINT32)wcslen(cls), &name))) {
        RoGetActivationFactory(name, IID_LEDeviceStatics2, (void**)&statics);
        WindowsDeleteString(name);
    }
    if (!statics) return -1;

    int level = -1;
    IAsyncOp* op = nullptr;
    IInspectable* device = nullptr;
    ILEDevice3* device3 = nullptr;
    IInspectable* servicesResult = nullptr;
    IInspectable* service = nullptr;
    IGattDeviceService3Abi* service3 = nullptr;
    IInspectable* charsResult = nullptr;
    IInspectable* ch = nullptr;

    // Null until the watcher has heard this address (a fresh one appeared about 1 s after it started).
    for (int i = 0; i < kLookupTries && !device; i++) {
        if (i) Sleep(500);
        if (SUCCEEDED(statics->FromBluetoothAddressWithTypeAsync(leAddress, addressType, &op))) device = Await(op, 5000);
        op = nullptr;
    }
    if (device && SUCCEEDED(device->QueryInterface(IID_LEDevice3, (void**)&device3)) && device3 &&
        SUCCEEDED(device3->GetGattServicesForUuidWithCacheModeAsync(kBatteryService, kCacheModeUncached, &op)))
        servicesResult = Await(op, 15000);
    op = nullptr;
    service = FirstItem(servicesResult);
    if (service && SUCCEEDED(service->QueryInterface(IID_GattDeviceService3, (void**)&service3)) && service3 &&
        SUCCEEDED(service3->GetCharacteristicsForUuidWithCacheModeAsync(kBatteryLevel, kCacheModeUncached, &op)))
        charsResult = Await(op, 10000);
    op = nullptr;
    ch = FirstItem(charsResult);
    if (ch) {
        // The first value after connecting can be a placeholder 100%: read again once it has settled.
        auto* c = (IGattCharacteristicAbi*)ch;  // default interface of GattCharacteristic
        level = ReadLevelOnce(c);
        if (settle && level >= 0) {
            Sleep(kSettleMs);
            level = ReadLevelOnce(c);
        }
    }

    Release(ch);
    Release(charsResult);
    Release(service3);
    CloseAndRelease(service);
    Release(servicesResult);
    Release(device3);
    CloseAndRelease(device);  // drops the LE connection
    Release(statics);
    return level;
}

DWORD WINAPI QueryThread(LPVOID) {
    std::vector<Speaker> list;
    {
        std::lock_guard<std::mutex> lock(g_state.m);
        list.swap(g_state.pending);
    }
    HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    for (const Speaker& s : list) {
        int level = -1;
        FastPairStream::Info info;
        if (StartWinsock() && ReadStreamInfo(s.addr, info) && KnownModel(info.modelId)) level = ReadBatteryLevel(info.leAddress);
        std::lock_guard<std::mutex> lock(g_state.m);
        if (level >= 0) {
            Entry& e = g_state.entries[s.addr];
            e.percent = level;
            e.lastOk = GetTickCount64();
        }
    }
    if (SUCCEEDED(init)) RoUninitialize();
    std::lock_guard<std::mutex> lock(g_state.m);
    g_state.running = false;
    return 0;
}

}  // namespace

int ReadLeBatteryLevel(UINT64 address, bool randomAddress, bool settle) {
    return ReadBatteryLevel(address, randomAddress ? kAddressTypeRandom : 0, settle);
}

void PollMarshall(PollContext& ctx) {
    std::vector<Speaker> speakers = FindConnectedSpeakers();
    if (speakers.empty()) return;

    HANDLE first = nullptr;  // the very first read is waited for (app start, --list)
    {
        ULONGLONG now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_state.m);
        if (!g_state.running) {
            std::vector<Speaker> due;
            bool isFirst = false;
            for (const Speaker& s : speakers) {
                Entry& e = g_state.entries[s.addr];
                ULONGLONG wait = e.lastOk && e.lastOk >= e.lastAttempt ? kQueryEveryMs : kRetryMs;
                if (e.lastAttempt && now - e.lastAttempt < wait) continue;
                isFirst |= !e.lastAttempt;
                e.lastAttempt = now;
                due.push_back(s);
            }
            if (!due.empty()) {
                // On this (poll) thread: EnsureBleWatcher is not thread-safe, and PollBluetooth calls it here too.
                EnsureBleWatcher();
                g_state.pending = due;
                if (HANDLE t = CreateThread(nullptr, 0, QueryThread, nullptr, 0, nullptr)) {
                    g_state.running = true;
                    if (isFirst) first = t;
                    else CloseHandle(t);
                }
            }
        }
    }
    if (first) {
        // One absolute 4 s deadline, once per speaker, so the first refresh (and --list) can show the
        // level when the speaker answers quickly. Later polls never wait; a slower read finishes on its
        // thread and shows on the next refresh.
        WaitForSingleObject(first, 4000);
        CloseHandle(first);
    }

    std::lock_guard<std::mutex> lock(g_state.m);
    ULONGLONG now = GetTickCount64();
    for (const Speaker& sp : speakers) {
        const Entry& e = g_state.entries[sp.addr];
        if (!e.lastOk || now - e.lastOk >= kStaleMs) continue;
        DeviceStatus s;
        s.key = L"marshall:" + sp.addrText;
        s.name = sp.name;
        s.kind = DeviceKind::Speaker;
        s.via = L"Bluetooth";
        s.percent = e.percent;
        s.graceSec = 600;
        ctx.out.push_back(s);
    }
}
