// Exercise Pulsar B4 status-query decoding without USB hardware.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <map>

namespace {
std::map<std::wstring, std::vector<std::vector<BYTE>>> reports;
int readOnlyOpens = 0;
int writeOpens = 0;
int lastReadTimeout = 0;
int writeCalls = 0;
bool writeSucceeds = true;
std::vector<BYTE> lastWrite;

HidColl ProDongle(const wchar_t* path) {
    HidColl c;
    c.path = path;
    c.vid = 0x3710;
    c.pid = 0x5405;
    c.caps.UsagePage = 0x0001;
    c.caps.Usage = 0x0000;
    c.caps.InputReportByteLength = 65;
    c.caps.OutputReportByteLength = 65;
    return c;
}

HidColl WiredMouse(const wchar_t* path) {
    HidColl c = ProDongle(path);
    c.pid = 0x7401;
    return c;
}

std::vector<BYTE> Battery(BYTE percent, BYTE charging = 0, BYTE marker = 0x84) {
    return { 0xB4, percent, 0x12, 0x34, marker, 0x40, charging };
}

const DeviceStatus* FindStatus(const PollContext& ctx, const wchar_t* key) {
    for (const DeviceStatus& status : ctx.out)
        if (status.key == key) return &status;
    return nullptr;
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    ++writeOpens;
    return true;
}
bool HidDev::OpenReadOnly(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    ++readOnlyOpens;
    return true;
}
bool HidDev::OpenFeatureOnly(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 1000);
    lastReadTimeout = timeoutMs;
    auto& queue = reports[c_.path];
    if (queue.empty()) return -1;
    out = queue.front();
    queue.erase(queue.begin());
    return (int)out.size();
}
bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 1000);
    ++writeCalls;
    lastWrite = data;
    return writeSucceeds;
}
int HidDev::WriteAndRead(const std::vector<BYTE>& data, std::vector<BYTE>& out, int timeoutMs,
                         bool* writeSucceeded) {
    if (writeSucceeded) *writeSucceeded = false;
    if (!Write(data, timeoutMs)) return -1;
    if (writeSucceeded) *writeSucceeded = true;
    return Read(out, timeoutMs);
}
bool HidDev::SetFeature(const std::vector<BYTE>&) { return false; }
bool HidDev::GetFeature(BYTE, std::vector<BYTE>&) { return false; }
std::wstring HidProduct(const HidColl& c) {
    return c.pid == 0x7401 ? L"Pulsar ZYWOO The Chosen X" : L"Pulsar PRO Dongle";
}
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    PollContext ctx;
    ctx.colls = { ProDongle(L"pro-dongle") };

    reports = { { L"pro-dongle", { { 0xBB, 0 }, Battery(100) } } };
    PollPulsar(ctx);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == L"pulsar:5405" && ctx.out[0].name == L"Pulsar wireless mouse");
    assert(ctx.out[0].kind == DeviceKind::Mouse && ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.out[0].percent == 100 && !ctx.out[0].charging && ctx.out[0].connected);
    assert(ctx.out[0].graceSec == 1800);
    assert(readOnlyOpens == 0 && writeOpens == 1 && writeCalls == 1);
    assert(lastWrite.size() == 65 && lastWrite[0] == 0 && lastWrite[1] == 0xB4);
    for (size_t i = 2; i < lastWrite.size(); ++i) assert(lastWrite[i] == 0);
    assert(ctx.IsClaimed(0x3710, 0x5405));

    ctx = {};
    ctx.colls = { ProDongle(L"pro-dongle") };
    reports = { { L"pro-dongle", { Battery(100, 1) } } };
    PollPulsar(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].charging);
    assert(readOnlyOpens == 0 && writeOpens == 1 && writeCalls == 2);

    ctx = {};
    ctx.colls = { ProDongle(L"no-first-report") };
    reports.clear();
    PollPulsar(ctx);
    const DeviceStatus* sleepingDongle = FindStatus(ctx, L"pulsar:5405");
    assert(ctx.out.size() == 1 && sleepingDongle && sleepingDongle->connected);
    assert(sleepingDongle->asleep && sleepingDongle->percent == -1 && sleepingDongle->graceSec == 1800);
    assert(writeOpens == 2 && writeCalls == 3 && lastReadTimeout > 0);

    ctx = {};
    ctx.colls = { ProDongle(L"no-first-report") };
    reports = { { L"no-first-report", { Battery(75) } } };
    writeSucceeds = false;
    PollPulsar(ctx);
    sleepingDongle = FindStatus(ctx, L"pulsar:5405");
    assert(sleepingDongle && sleepingDongle->asleep && sleepingDongle->connected);
    assert(writeCalls == 4 && writeOpens == 3);

    ctx = {};
    ctx.colls = { ProDongle(L"no-first-report") };
    writeSucceeds = true;
    PollPulsar(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 75 && readOnlyOpens == 0);
    assert(writeOpens == 4 && writeCalls == 5);

    ctx = {};
    ctx.colls = { ProDongle(L"no-first-report") };
    reports = { { L"no-first-report", { { 0xB4, 101, 0, 0, 0x84, 0x40, 0 },
                                        { 0xB4, 40, 0, 0, 0, 0, 0 },
                                        Battery(0) } } };
    PollPulsar(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0 && !ctx.out[0].charging);

    ctx = {};
    ctx.colls = { ProDongle(L"wrong-page") };
    ctx.colls[0].caps.UsagePage = 0xFF00;
    reports.clear();
    readOnlyOpens = 0;
    writeOpens = 0;
    writeCalls = 0;
    PollPulsar(ctx);
    assert(readOnlyOpens == 0 && writeOpens == 0 && ctx.out.empty() && ctx.claimed.empty());

    ctx = {};
    ctx.colls = { ProDongle(L"wrong-size") };
    ctx.colls[0].caps.InputReportByteLength = 64;
    PollPulsar(ctx);
    assert(readOnlyOpens == 0 && writeOpens == 0 && ctx.out.empty() && ctx.claimed.empty());

    ctx = {};
    ctx.colls = { WiredMouse(L"zywoo-mouse") };
    reports = { { L"zywoo-mouse", { Battery(100, 1, 0x86) } } };
    PollPulsar(ctx);
    const DeviceStatus* wired = FindStatus(ctx, L"pulsar:7401");
    const DeviceStatus* disconnectedDongle = FindStatus(ctx, L"pulsar:5405");
    assert(ctx.out.size() == 2 && wired && disconnectedDongle);
    assert(wired->name == L"Pulsar ZYWOO The Chosen X" && wired->via == L"USB cable");
    assert(wired->kind == DeviceKind::Mouse && wired->percent == 100 && wired->charging);
    assert(!disconnectedDongle->connected && !disconnectedDongle->asleep);
    assert(readOnlyOpens == 0 && writeOpens == 1 && writeCalls == 1);
    assert(lastWrite.size() == 65 && lastWrite[0] == 0 && lastWrite[1] == 0xB4);
    assert(ctx.IsClaimed(0x3710, 0x7401));

    ctx = {};
    ctx.colls = { WiredMouse(L"zywoo-mouse") };
    reports = { { L"zywoo-mouse", { { 0xB4, 0, 0, 0, 0, 0, 1 }, Battery(100, 1, 0x85) } } };
    PollPulsar(ctx);
    wired = FindStatus(ctx, L"pulsar:7401");
    assert(wired && wired->percent == 100 && wired->charging);
    assert(writeOpens == 1 && writeCalls == 2);

    ctx = {};
    ctx.colls = { ProDongle(L"pro-dongle-marker85") };
    reports = { { L"pro-dongle-marker85", { Battery(100, 0, 0x85) } } };
    const int opensBeforeDongleVariant = writeOpens;
    const int writesBeforeDongleVariant = writeCalls;
    PollPulsar(ctx);
    const DeviceStatus* dongle = FindStatus(ctx, L"pulsar:5405");
    const DeviceStatus* disconnectedMouse = FindStatus(ctx, L"pulsar:7401");
    assert(ctx.out.size() == 2 && dongle && disconnectedMouse);
    assert(dongle->percent == 100 && !dongle->charging && dongle->connected);
    assert(!disconnectedMouse->connected && !disconnectedMouse->asleep);
    assert(writeOpens == opensBeforeDongleVariant + 1 && writeCalls == writesBeforeDongleVariant + 1);

    ctx = {};
    reports.clear();
    PollPulsar(ctx);
    disconnectedDongle = FindStatus(ctx, L"pulsar:5405");
    disconnectedMouse = FindStatus(ctx, L"pulsar:7401");
    assert(ctx.out.size() == 2 && disconnectedDongle && disconnectedMouse);
    assert(!disconnectedDongle->connected && !disconnectedDongle->asleep);
    assert(!disconnectedMouse->connected && !disconnectedMouse->asleep);

    ctx = {};
    ctx.colls = { ProDongle(L"pro-dongle-reconnected") };
    reports.clear();
    PollPulsar(ctx);
    dongle = FindStatus(ctx, L"pulsar:5405");
    disconnectedMouse = FindStatus(ctx, L"pulsar:7401");
    assert(ctx.out.size() == 2 && dongle && disconnectedMouse);
    assert(dongle->connected && dongle->percent == -1 && dongle->asleep);
    assert(!disconnectedMouse->connected && !disconnectedMouse->asleep);

    ctx = {};
    ctx.colls = { ProDongle(L"pro-dongle-reconnected") };
    reports = { { L"pro-dongle-reconnected", { Battery(100, 0, 0x85) } } };
    PollPulsar(ctx);
    dongle = FindStatus(ctx, L"pulsar:5405");
    disconnectedMouse = FindStatus(ctx, L"pulsar:7401");
    assert(ctx.out.size() == 2 && dongle && disconnectedMouse);
    assert(dongle->connected && dongle->percent == 100 && !dongle->asleep);
    assert(!disconnectedMouse->connected && !disconnectedMouse->asleep);

    std::puts("Pulsar B4 devices: dongle and wired mouse status, charging, invalid reports, matching OK");
}
