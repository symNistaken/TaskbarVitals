// Sony INZONE headsets, all speaking the Sony HCI command layer (see sony_inzone_protocol.h):
//   H9 II (054C:0FA8), H5 (054C:0EBF) and the INZONE Buds dongle (054C:0EC2): over HID, report 02
//   (64 bytes) on the vendor collection with usage page FF04.
//   H9 / H7, first generation (054C:0E53): over the dongle's USB serial port, 460800 8N1. INZONE
//   Hub keeps that port open while it runs, so the headset is only read while INZONE Hub is closed;
//   in between the last level stays on screen, dimmed, for up to an hour.
// Battery = event 04, type 01 (GET) to the headset (addr 0x41).
//   Headsets: reply payload [charging][percent], percent FF = headset off.
//   Buds:     reply payload [status][percent] for the left bud, right bud, then case; percent FF =
//             bud is in the case. With both buds in the case the dongle does not respond. No
//             charging flag.
// Protocol from github.com/Sapd/HeadsetControl (sony_inzone_protocol.hpp, sony_inzone_buds.hpp)
// and INZONE Hub 1.0.20 (serial transport of the first-generation dongle).
// Buds reply layout and left-first order from github.com/kinako19/inzone-buds-battery-tray and
// github.com/takamachi66/inzone-buds-battery-tray (docs/protocol*.md).
#include "drivers.h"
#include "sony_inzone_protocol.h"

#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace {

using SonyHci::Event;

// HID input report: [02][packet length][packet].
bool ParseEvent(const std::vector<BYTE>& b, Event& e) {
    if (b.size() < 15 || b[0] != 0x02) return false;
    int len = b[1];
    if (len < 12 || len > 62 || (int)b.size() < len + 2) return false;
    return SonyHci::ParseEvent(b.data() + 2, len, e);
}

USHORT NextTid() {
    static USHORT tid = 1;
    if (++tid <= 1) tid = 2;  // tid 1 is used by the dongle's own notifications
    return tid;
}

bool IsBatteryReply(const Event& e, USHORT tid, size_t minPayload) {
    if (e.id != SonyHci::kBatteryInfo || e.tid != tid || e.payload.size() < minPayload) return false;
    return e.type == SonyHci::kRet || e.type == SonyHci::kNotifyActive;  // RET or active notification
}

enum class Reply { Ok, NoReply, Failed };

// NoReply/Failed are identical for headset
// For buds, NoReply can imply both buds are in the case
Reply GetBattery(HidDev& d, size_t minPayload, std::vector<BYTE>& payload) {
    USHORT tid = NextTid();
    std::vector<BYTE> req(64, 0);
    std::vector<BYTE> packet = SonyHci::BatteryGet(tid);
    req[0] = 0x02;
    req[1] = (BYTE)packet.size();
    std::copy(packet.begin(), packet.end(), req.begin() + 2);
    d.Flush();
    if (!d.Write(req)) return Reply::Failed;
    ULONGLONG end = GetTickCount64() + 1500;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 15) return Reply::NoReply;
        Event e;
        if (!ParseEvent(r, e) || !IsBatteryReply(e, tid, minPayload)) continue;
        payload = std::move(e.payload);
        return Reply::Ok;
    }
    return Reply::NoReply;
}

int Level(BYTE value) { return value <= 100 ? value : -1; }

std::wstring Pct(int value) { return value >= 0 ? std::to_wstring(value) + L"%" : L"-"; }

// Headset battery reply [charging][percent] -> status; percent FF = headset off.
void SetHeadsetBattery(DeviceStatus& s, const std::vector<BYTE>& p) {
    bool offline = p[1] == 0xFF;
    s.connected = !offline;
    s.percent = offline ? -1 : (p[1] <= 100 ? p[1] : -1);
    s.charging = p[0] != 0;
}

// COM port names of the first-generation H9 / H7 dongle (054C:0E53, interface 06).
std::vector<std::wstring> FindSerialDongles() {
    std::vector<std::wstring> out;
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512];
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        std::wstring id = inst;
        for (auto& ch : id) ch = towupper(ch);
        if (id.find(L"VID_054C&PID_0E53") == std::wstring::npos) continue;
        HKEY k = SetupDiOpenDevRegKey(di, &dd, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (k == INVALID_HANDLE_VALUE) continue;
        wchar_t name[32] = {};
        DWORD sz = sizeof(name) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, L"PortName", nullptr, &type, (BYTE*)name, &sz) == ERROR_SUCCESS && type == REG_SZ)
            out.push_back(name);
        RegCloseKey(k);
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}

// Battery GET over the serial port. False when the port is in use (INZONE Hub open), or nothing
// answered.
bool GetSerialBattery(const std::wstring& port, std::vector<BYTE>& payload) {
    HANDLE h = CreateFileW((L"\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = MAXDWORD;  // with the multiplier below: return as soon as bytes arrive
    to.ReadTotalTimeoutMultiplier = MAXDWORD;
    to.ReadTotalTimeoutConstant = 300;
    to.WriteTotalTimeoutConstant = 1000;
    if (GetCommState(h, &dcb)) {
        dcb.BaudRate = 460800;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fOutX = dcb.fInX = FALSE;
        ok = SetCommState(h, &dcb) && SetCommTimeouts(h, &to);
    }
    USHORT tid = NextTid();
    if (ok) {
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
        std::vector<BYTE> req = SonyHci::BatteryGet(tid);
        DWORD n = 0;
        ok = WriteFile(h, req.data(), (DWORD)req.size(), &n, nullptr) && n == req.size();
    }
    bool found = false;
    std::vector<BYTE> buf;
    ULONGLONG end = GetTickCount64() + 1500;
    while (ok && !found && (LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        BYTE chunk[256];
        DWORD n = 0;
        if (!ReadFile(h, chunk, sizeof(chunk), &n, nullptr)) break;
        buf.insert(buf.end(), chunk, chunk + n);
        Event e;
        while (!found && SonyHci::NextSerialEvent(buf, e)) {
            if (!IsBatteryReply(e, tid, 2)) continue;  // other notifications from the dongle
            payload = std::move(e.payload);
            found = true;
        }
    }
    CloseHandle(h);
    return found;
}

}  // namespace

void PollSonyInzone(PollContext& ctx) {
    // minPayload: headsets reply [charging][percent];
    // buds reply 3x [status][percent] for both buds + case.
    struct Model { USHORT pid; const wchar_t* name; bool buds; size_t minPayload; };
    for (const Model& m : { Model{ 0x0FA8, L"Sony INZONE H9 II", false, 2 },
                            Model{ 0x0EBF, L"Sony INZONE H5", false, 2 },
                            Model{ 0x0EC2, L"Sony INZONE Buds", true, 6 } }) {
        if (ctx.IsClaimed(0x054C, m.pid)) continue;
        auto status = [&] {
            DeviceStatus s;
            s.key = L"inzone:" + Hex4(m.pid);
            s.name = m.name;
            s.kind = DeviceKind::Headset;
            s.via = L"2.4 GHz receiver";
            return s;
        };
        bool added = false, noReply = false;
        auto filter = [](const HidColl& c) { return c.page() == 0xFF04 && c.outLen() >= 64; };
        for (const HidColl* c : ctx.Find(0x054C, m.pid, filter)) {
            HidDev d;
            if (!d.Open(*c)) continue;
            std::vector<BYTE> p;
            Reply reply = GetBattery(d, m.minPayload, p);
            if (reply == Reply::NoReply) noReply = true;
            if (reply != Reply::Ok) continue;
            DeviceStatus s = status();
            if (m.buds) {
                int left = Level(p[1]);
                int right = Level(p[3]);
                int caseLevel = Level(p[5]);
                s.percent = left < 0 || right < 0 ? std::max(left, right) : std::min(left, right);
                s.connected = s.percent >= 0;
                if (s.connected) {
                    s.detail = L"L " + Pct(left) + L", R " + Pct(right);
                    if (caseLevel >= 0) s.detail += L", case " + Pct(caseLevel);
                }
            } else {
                SetHeadsetBattery(s, p);
            }
            ctx.Add(s, 0x054C, m.pid);
            added = true;
            break;
        }
        // Buds: the dongle took the request but no collection answered, which is what it does with both
        // buds in the case. Add them as not connected (hidden) rather than treating the dongle as missing.
        if (m.buds && noReply && !added) {
            DeviceStatus s = status();
            s.connected = false;
            ctx.Add(s, 0x054C, m.pid);
        }
    }

    // First-generation H9 / H7: same packets over the dongle's serial port. Both headsets share the
    // dongle id and the windows device name ("INZONE H9 / INZONE H7"), so the name covers both.
    if (!ctx.IsClaimed(0x054C, 0x0E53)) {
        for (const std::wstring& port : FindSerialDongles()) {
            std::vector<BYTE> p;
            if (!GetSerialBattery(port, p)) continue;  // INZONE Hub has the port, or no answer
            DeviceStatus s;
            s.key = L"inzone:0E53";
            s.name = L"Sony INZONE H9 / H7";
            s.kind = DeviceKind::Headset;
            s.via = L"2.4 GHz receiver";
            SetHeadsetBattery(s, p);
            // While INZONE Hub holds the port nothing is reported; keep the last level (dimmed) meanwhile.
            s.graceSec = 3600;
            ctx.Add(s, 0x054C, 0x0E53);
            break;
        }
    }
}
