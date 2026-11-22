// Production Jabra GNP poller with a simulated HID transport: a GNP device that answers per address,
// like a headset on its USB cable (08) or behind a Link dongle (04).
#include "../src/drivers/drivers.h"

#include <cstdio>
#include <deque>
#include <map>
#include <string>

void PollJabra(PollContext& ctx);

namespace {
struct Peer {
    std::vector<BYTE> battery;  // [flags][level]; empty = NAK with `nak`
    BYTE nak = 0xF5;            // 0 = no reply at all
    std::string name;
};

int failures = 0, opens = 0;
std::map<BYTE, Peer> peers;
std::vector<std::vector<BYTE>> writes;
std::deque<std::vector<BYTE>> pending;
std::vector<std::wstring> opened;
std::wstring product;
std::wstring nakAllOn;  // collection path that NAKs every request
const char* current = "";

void Check(bool result, const char* expression, int line) {
    if (!result) { std::printf("FAIL %s:%d %s\n", current, line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

std::vector<BYTE> Frame(BYTE src, BYTE seq, BYTE cls, BYTE op, const std::vector<BYTE>& payload) {
    std::vector<BYTE> f(64, 0);
    f[0] = 0x05;
    f[2] = src;
    f[3] = seq;
    f[4] = (BYTE)(0xC0 | (6 + payload.size()));
    f[5] = cls;
    f[6] = op;
    std::copy(payload.begin(), payload.end(), f.begin() + 7);
    return f;
}

HidColl Coll(USHORT pid, USHORT usage, const wchar_t* path) {
    HidColl c;
    c.vid = 0x0B0E;
    c.pid = pid;
    c.iface = 3;
    c.path = path;
    c.caps.UsagePage = 0xFF00;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}

PollContext Context(std::vector<HidColl> colls, const wchar_t* name) {
    opens = 0;
    writes.clear();
    pending.clear();
    opened.clear();
    product = name;
    PollContext ctx;
    HidColl audio = colls.front();  // the headset's other collections are left alone
    audio.caps.UsagePage = 0x000C;
    audio.caps.Usage = 0x0001;
    audio.path = L"consumer";
    ctx.colls.push_back(audio);
    for (auto& c : colls) ctx.colls.push_back(c);
    return ctx;
}

int AddressesAsked() {
    int mask = 0;
    for (auto& w : writes) mask |= 1 << w[1];
    return mask;
}
}  // namespace

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    opened.push_back(c.path);
    CHECK(c.page() == 0xFF00);
    return true;
}
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    writes.push_back(data);
    if (c_.path == nakAllOn) {
        pending.push_back(Frame(data[1], data[3], 0xFE, 0xFF, {}));
        return true;
    }
    auto it = peers.find(data[1]);
    if (it == peers.end()) return true;
    const Peer& peer = it->second;
    BYTE seq = data[3], cls = data[5], op = data[6];
    if (cls == 0x02 && op == 0x00 && !peer.name.empty()) {
        std::vector<BYTE> p{ (BYTE)peer.name.size() };
        p.insert(p.end(), peer.name.begin(), peer.name.end());
        pending.push_back(Frame(data[1], seq, cls, op, p));
    } else if (cls == 0x12 && op == 0x02 && !peer.battery.empty()) {
        pending.push_back(Frame(data[1], seq, cls, op, peer.battery));
    } else if (peer.nak) {
        pending.push_back(Frame(data[1], seq, 0xFE, peer.nak, {}));
    }
    return true;
}
int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    CHECK(timeoutMs > 0 && timeoutMs <= 700);
    if (pending.empty()) return -1;
    buf = pending.front();
    pending.pop_front();
    return (int)buf.size();
}
std::wstring HidProduct(const HidColl&) { return product; }
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    current = "Evolve3 75 on its USB cable: asked at 08 on the usage 05 collection";
    {
        peers = { { 0x08, { { 0x01, 95 }, 0xF5, "Jabra Evolve3 75" } } };
        auto ctx = Context({ Coll(0x2548, 0x01, L"col01"), Coll(0x2548, 0x05, L"col05") }, L"Jabra Evolve3 75");
        PollJabra(ctx);
        CHECK(ctx.out.size() == 1 && ctx.IsClaimed(0x0B0E, 0x2548));
        CHECK(!opened.empty() && opened[0] == L"col05");
        CHECK(AddressesAsked() == (1 << 8));
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 95 && s.charging && s.connected);
            CHECK(s.name == L"Jabra Evolve3 75" && s.key == L"jabra:2548");
            CHECK(s.via == L"USB cable" && s.kind == DeviceKind::Headset);
        }
    }

    current = "Link 390 with a headset: asked at 04 first, by its product string";
    {
        peers = { { 0x04, { { 0x00, 60 }, 0xF5, "Jabra Evolve2 65 Flex" } }, { 0x08, { {}, 0 } } };
        auto ctx = Context({ Coll(0x2E51, 0x05, L"link") }, L"Jabra Link 390");
        PollJabra(ctx);
        CHECK(ctx.out.size() == 1);
        CHECK(AddressesAsked() == (1 << 4));
        if (ctx.out.size() == 1) {
            const auto& s = ctx.out[0];
            CHECK(s.percent == 60 && !s.charging && s.connected);
            CHECK(s.name == L"Jabra Evolve2 65 Flex" && s.via == L"Jabra Link dongle");
        }
    }

    current = "Link 390, headset switched off: shown disconnected with the last name";
    {
        peers = { { 0x04, { {}, 0xF3 } } };
        auto ctx = Context({ Coll(0x2E51, 0x05, L"link") }, L"Jabra Link 390");
        PollJabra(ctx);
        CHECK(ctx.out.size() == 1);
        if (ctx.out.size() == 1) CHECK(!ctx.out[0].connected && ctx.out[0].name == L"Jabra Evolve2 65 Flex");
    }

    current = "unlisted dongle without Link in its name: 08 silent, falls back to 04";
    {
        peers = { { 0x04, { { 0x00, 40 }, 0xF5, "Jabra Evolve2 85" } }, { 0x08, { {}, 0 } } };
        auto ctx = Context({ Coll(0x24C9, 0x01, L"dongle") }, L"Jabra USB");
        PollJabra(ctx);
        CHECK(ctx.out.size() == 1 && AddressesAsked() == ((1 << 8) | (1 << 4)));
        if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == 40 && ctx.out[0].via == L"Jabra Link dongle");

        writes.clear();
        PollContext again = Context({ Coll(0x24C9, 0x01, L"dongle") }, L"Jabra USB");
        PollJabra(again);  // the address that answered is kept
        CHECK(again.out.size() == 1 && AddressesAsked() == (1 << 4));
    }

    current = "wired headset without a battery: NAKed at both addresses, not asked again";
    {
        peers = { { 0x08, { {}, 0xFF } }, { 0x04, { {}, 0xF5 } } };
        auto ctx = Context({ Coll(0x2510, 0x01, L"wired") }, L"Jabra Evolve2 40");
        PollJabra(ctx);
        CHECK(ctx.out.empty() && !ctx.IsClaimed(0x0B0E, 0x2510));
        auto again = Context({ Coll(0x2510, 0x01, L"wired") }, L"Jabra Evolve2 40");
        PollJabra(again);
        CHECK(again.out.empty() && opens == 0);
    }

    current = "one collection NAKs everything, the other answers: not skipped later";
    {
        peers = { { 0x08, { { 0x00, 70 }, 0xF5, "Jabra Evolve3 85" } } };
        nakAllOn = L"e85-col05";
        auto colls = std::vector<HidColl>{ Coll(0x2550, 0x05, L"e85-col05"), Coll(0x2550, 0x01, L"e85-col01") };
        auto ctx = Context(colls, L"Jabra Evolve3 85");
        PollJabra(ctx);
        CHECK(ctx.out.size() == 1 && opens == 2);
        auto again = Context(colls, L"Jabra Evolve3 85");
        PollJabra(again);
        CHECK(again.out.size() == 1);
        nakAllOn.clear();
    }

    current = "nothing answers: not asked again on the next poll";
    {
        peers = { { 0x08, { {}, 0 } }, { 0x04, { {}, 0 } } };
        auto ctx = Context({ Coll(0x2560, 0x01, L"silent") }, L"Jabra Speak");
        PollJabra(ctx);
        CHECK(ctx.out.empty() && opens == 1 && AddressesAsked() == ((1 << 8) | (1 << 4)));
        auto again = Context({ Coll(0x2560, 0x01, L"silent") }, L"Jabra Speak");
        PollJabra(again);
        CHECK(again.out.empty() && opens == 0);
    }

    current = "DECT base and other collections are left alone";
    {
        peers = { { 0x08, { { 0x00, 50 }, 0xF5, "Jabra Engage 75" } } };
        auto ctx = Context({ Coll(0x1112, 0x01, L"engage") }, L"Jabra Engage 75");
        PollJabra(ctx);
        CHECK(ctx.out.empty() && opens == 0);

        HidColl other = Coll(0x2548, 0x02, L"other-usage");
        auto ctx2 = Context({ other }, L"Jabra Evolve3 75");
        PollJabra(ctx2);
        CHECK(ctx2.out.empty() && opens == 0);
    }

    current = "battery level above 100 is unknown";
    {
        peers = { { 0x08, { { 0x00, 200 }, 0xF5, "Jabra Evolve2 55" } } };
        auto ctx = Context({ Coll(0x2530, 0x01, L"e55") }, L"Jabra Evolve2 55");
        PollJabra(ctx);
        CHECK(ctx.out.size() == 1);
        if (ctx.out.size() == 1) CHECK(ctx.out[0].percent == -1);
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("jabra driver: all tests passed\n");
    return 0;
}
