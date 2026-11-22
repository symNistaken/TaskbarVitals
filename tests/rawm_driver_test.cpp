// Simulated RAWM receivers and mice exercising PollRawm end to end: the receiver-then-channel-0 query,
// sleeping mice, cable + receiver deduplication, strict matching and the absolute deadline.
// No hardware access; the fake device only answers while the host keeps writing, like the real one.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <deque>
#include <map>
#include <string>

namespace {

struct Fake {
    bool receiver = true;     // false: a mouse on its USB cable
    bool answers = true;      // the USB device itself answers CMD_QUERY
    bool mouseAwake = true;   // the mouse behind a receiver answers on channel 0
    std::string ownInfo;      // JSON the USB device returns
    std::string mouseInfo;    // JSON the mouse returns on channel 0
    std::deque<std::vector<BYTE>> queued, readable;
};

std::map<USHORT, Fake> fakes;
int opens = 0, writes = 0;

std::vector<BYTE> Event(BYTE cmd, const std::string& text) {
    std::vector<BYTE> body(text.begin(), text.end());
    body.push_back(0);
    size_t len = body.size() + 2;
    std::vector<BYTE> e{ 0xFF, 0xFF, 0xFF, 0xFF, (BYTE)(cmd | ((len >> 4) & 0xF0)), (BYTE)len };
    e.insert(e.end(), body.begin(), body.end());
    return e;
}

// Input reports as HidDev::Read returns them (report ID dropped), padded to 64 bytes.
void Queue(Fake& f, const std::vector<BYTE>& stream, size_t chunk, int channel) {
    for (size_t o = 0; o < stream.size(); o += chunk) {
        size_t n = std::min(chunk, stream.size() - o);
        std::vector<BYTE> r;
        if (channel >= 0) r.push_back((BYTE)(0xC0 | channel));
        r.push_back((BYTE)(0x80 | n));
        r.insert(r.end(), stream.begin() + o, stream.begin() + o + n);
        r.resize(64, 0);
        f.queued.push_back(r);
    }
}

HidColl Coll(USHORT vid, USHORT pid, USHORT page = 0xFF00, USHORT usage = 1, USHORT len = 65) {
    HidColl c;
    c.vid = vid;
    c.pid = pid;
    c.iface = 2;
    c.path = L"test";
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = len;
    c.caps.OutputReportByteLength = len;
    return c;
}

std::string Receiver(int pid) {
    return "{\"r\":\"2.0.2\",\"rc\":4,\"dn\":\"RAWM HS Receiver\",\"pi\":" + std::to_string(pid) + ",\"vi\":6421,\"cn\":0}";
}

std::string Mouse(const char* name, int pid, int battery, int chr) {
    return std::string("{\"r\":\"1.2.0\",\"rc\":6,\"dn\":\"") + name + "\",\"pi\":" + std::to_string(pid) +
           ",\"vi\":6421,\"battery\":" + std::to_string(battery) + ",\"chr\":" + std::to_string(chr) +
           ",\"cpi\":1600,\"polling\":1000,\"crc\":1,\"sst\":\"PAW3950\"}";
}

void Reset() {
    fakes.clear();
    opens = writes = 0;
}

}  // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = true;
    assert(fakes.count(c.pid));
    return true;
}

void HidDev::Close() {
    h_ = INVALID_HANDLE_VALUE;
}

void HidDev::Flush() {}

bool HidDev::Write(const std::vector<BYTE>& data, int) {
    Fake& f = fakes.at(c_.pid);
    ++writes;
    // Only RAWM HUB's connect-time requests may be sent: CMD_QUERY and CMD_PING.
    assert(data.size() >= 3 && data[0] == 0x00);
    size_t i = 1;
    int channel = -1;
    if ((data[i] & 0xC0) == 0xC0) channel = data[i++] & 0x3F;
    assert(channel == -1 || channel == 0);
    assert((data[i] & 0xC0) == 0x80);
    std::vector<BYTE> e(data.begin() + i + 1, data.end());
    assert(e.size() == (size_t)(data[i] & 0x3F));
    bool query = e.size() == 13 && e[0] == 0x01 && e[1] == 13 && e[2] == 0x03;
    bool ping = e == std::vector<BYTE>({ 0x0E, 0x04, 0x00, 0x00 });
    assert(query || ping);

    if (query && channel < 0 && f.answers) Queue(f, Event(0x02, f.ownInfo), 63, -1);
    if (query && channel == 0 && f.receiver && f.mouseAwake) Queue(f, Event(0x02, f.mouseInfo), 30, 0);
    // The device only sends while the host writes: release a few reports per packet.
    for (int n = 0; n < 4 && !f.queued.empty(); n++) {
        f.readable.push_back(f.queued.front());
        f.queued.pop_front();
    }
    return true;
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    Fake& f = fakes.at(c_.pid);
    if (f.readable.empty()) {
        Sleep((DWORD)std::min(timeoutMs, 20));
        return -1;
    }
    buf = f.readable.front();
    f.readable.pop_front();
    return (int)buf.size();
}

std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    // 1. Leviathan V4 behind the RAWM HS Receiver (1915:2346), the tested setup.
    Reset();
    fakes[0x2346] = { true, true, true, Receiver(0x2346), Mouse("LEVIATHAN V4", 0x234A, 82, 0) };
    PollContext ctx;
    ctx.colls = { Coll(0x1915, 0x2346, 0x0001, 2, 8), Coll(0x1915, 0x2346) };
    PollRawm(ctx);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == L"rawm:234A");
    assert(ctx.out[0].name == L"RAWM LEVIATHAN V4");
    assert(ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].percent == 82 && !ctx.out[0].charging);
    assert(ctx.out[0].graceSec == 1800 && ctx.out[0].vid == 0x1915);
    assert(ctx.IsClaimed(0x1915, 0x2346) && opens == 1);

    // 2. Receiver answers, mouse asleep: no entry (the UI keeps the last one, dimmed), the receiver is
    //    claimed so no later driver reports it, and the single deadline bounds the wait.
    Reset();
    fakes[0x2346] = { true, true, false, Receiver(0x2346), "" };
    ctx = {};
    ctx.colls = { Coll(0x1915, 0x2346) };
    ULONGLONG t0 = GetTickCount64();
    PollRawm(ctx);
    ULONGLONG elapsed = GetTickCount64() - t0;
    assert(ctx.out.empty() && ctx.IsClaimed(0x1915, 0x2346));
    assert(elapsed < 1500);

    // 3. Silent receiver (another app holds it, or not a RAWM HUB 2.0 device): nothing reported or claimed.
    Reset();
    fakes[0x232A] = { true, false, true, "", Mouse("ES21", 0x2328, 50, 0) };
    ctx = {};
    ctx.colls = { Coll(0x1915, 0x232A) };
    t0 = GetTickCount64();
    PollRawm(ctx);
    elapsed = GetTickCount64() - t0;
    assert(ctx.out.empty() && !ctx.IsClaimed(0x1915, 0x232A));
    assert(elapsed < 1500);

    // 4. Leviathan V4 on its USB cable (1915:234A), charging: the cable answers the query itself.
    Reset();
    fakes[0x234A] = { false, true, false, Mouse("LEVIATHAN V4", 0x234A, 47, 1), "" };
    ctx = {};
    ctx.colls = { Coll(0x1915, 0x234A) };
    PollRawm(ctx);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == L"rawm:234A" && ctx.out[0].via == L"USB cable");
    assert(ctx.out[0].percent == 47 && ctx.out[0].charging);

    // 5. Cable and receiver plugged in at once: one entry, with the cable's charging state.
    Reset();
    fakes[0x2346] = { true, true, true, Receiver(0x2346), Mouse("LEVIATHAN V4", 0x234A, 47, 0) };
    fakes[0x234A] = { false, true, false, Mouse("LEVIATHAN V4", 0x234A, 47, 1), "" };
    ctx = {};
    ctx.colls = { Coll(0x1915, 0x234A), Coll(0x1915, 0x2346) };
    PollRawm(ctx);
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].via == L"USB cable" && ctx.out[0].charging && ctx.out[0].percent == 47);
    assert(ctx.IsClaimed(0x1915, 0x2346) && ctx.IsClaimed(0x1915, 0x234A));

    // 6. Names that already carry the brand are kept; the SADES Dagger is not relabelled RAWM.
    Reset();
    fakes[0x232A] = { true, true, true, Receiver(0x232A), Mouse("SADES Dagger", 0x2329, 100, 0) };
    ctx = {};
    ctx.colls = { Coll(0x1915, 0x232A) };
    PollRawm(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"SADES Dagger" && ctx.out[0].key == L"rawm:2329");

    // 7. Info without a battery field: not reported.
    Reset();
    fakes[0x2346] = { true, true, true, Receiver(0x2346), "{\"dn\":\"LEVIATHAN V4\",\"pi\":9034}" };
    ctx = {};
    ctx.colls = { Coll(0x1915, 0x2346) };
    PollRawm(ctx);
    assert(ctx.out.empty());

    // 8. Strict matching: other Nordic products, other vendors and other collections are never opened.
    Reset();
    ctx = {};
    ctx.colls = {
        Coll(0x1915, 0xAE1C),                  // Ninjutso Sora V2 receiver
        Coll(0x1915, 0xAC1C),                  // Sprime PM1 receiver
        Coll(0x1915, 0x0723),                  // Darmoshark on a Nordic 1K receiver
        Coll(0x373E, 0x2346),                  // right PID, wrong vendor
        Coll(0x1915, 0x2346, 0xFF01, 1, 65),   // wrong usage page
        Coll(0x1915, 0x2346, 0xFF00, 1, 33),   // wrong report size
        Coll(0x1915, 0x2346, 0x0001, 2, 8),    // the mouse collection
    };
    PollRawm(ctx);
    assert(opens == 0 && writes == 0 && ctx.out.empty() && ctx.claimed.empty());

    std::puts("RAWM driver tests passed (receiver, sleeping mouse, silent receiver, cable, dedup, names, strict matching)");
    return 0;
}
