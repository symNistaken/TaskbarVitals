// Exercise the production Incott driver (PollMiscMice) with simulated collections; no hardware access.
// Frames are the ones captured on a first-generation G23 Pro behind its 8K receiver (093A:522C, FF05:0001).
#include "../src/drivers/drivers.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <thread>

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (0)

namespace {

struct FakeColl {
    std::map<BYTE, std::vector<BYTE>> replies;  // GET_FEATURE answer per last SET_FEATURE command
    std::deque<std::vector<BYTE>> inputs;      // queued input reports
    int lateAfterReads = -1;                   // deliver `late` after this many empty reads
    std::vector<BYTE> late;
    std::vector<BYTE> endless;                 // when set, every read returns this at once, forever
    BYTE last = 0;
};
std::map<std::wstring, FakeColl> fakes;  // by collection path
std::vector<BYTE> sent;                  // command byte of every SET_FEATURE

std::vector<BYTE> Pad(std::vector<BYTE> b, size_t n) {
    b.resize(n, 0);
    return b;
}

const std::vector<BYTE> kPush73 = Pad({ 0x09, 0x49, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33);
const std::vector<BYTE> kPush72 = Pad({ 0x09, 0x48, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33);
const std::vector<BYTE> kFeature58 = { 0x09, 0x89, 0x3A, 0x00, 0x00, 0x3B, 0x00, 0x00, 0x5A };
const std::vector<BYTE> kInfo8k = { 0x09, 0x8F, 0x01, 0x02, 0x02, 0xF0, 0xF1, 0x00, 0x02 };
const std::vector<BYTE> kInfo1k = { 0x09, 0x8F, 0x01, 0x02, 0x01, 0xF0, 0xF1, 0x00, 0x00 };
const std::vector<BYTE> kWired97 = { 0x09, 0x89, 0x61, 0x02, 0x03, 0x02, 0x00, 0x00, 0x5A };

HidColl Coll(USHORT pid, const wchar_t* path) {
    HidColl c;
    c.vid = 0x093A;
    c.pid = pid;
    c.path = path;
    c.iface = 3;
    c.caps.UsagePage = 0xFF05;
    c.caps.Usage = 1;
    c.caps.InputReportByteLength = 33;
    c.caps.OutputReportByteLength = 33;
    c.caps.FeatureReportByteLength = 9;
    return c;
}

PollContext Poll(std::vector<HidColl> colls) {
    sent.clear();
    PollContext ctx;
    ctx.colls = std::move(colls);
    PollMiscMice(ctx);
    return ctx;
}

const DeviceStatus* Find(const PollContext& ctx, const wchar_t* key) {
    for (const auto& s : ctx.out)
        if (s.key == key) return &s;
    return nullptr;
}

// Runs one poll; aborts the test (instead of hanging the suite) if it does not return within `ms`.
PollContext PollWithin(std::vector<HidColl> colls, ULONGLONG ms) {
    std::atomic<bool> done{ false };
    std::thread watchdog([&done, ms] {
        for (ULONGLONG t0 = GetTickCount64(); GetTickCount64() - t0 < ms; Sleep(10))
            if (done) return;
        std::fprintf(stderr, "CHECK failed: poll did not return within %llu ms\n", ms);
        std::fflush(stderr);
        std::_Exit(1);
    });
    PollContext ctx = Poll(std::move(colls));
    done = true;
    watchdog.join();
    return ctx;
}

int Count(BYTE cmd) {
    int n = 0;
    for (BYTE c : sent) n += c == cmd;
    return n;
}

}  // namespace

bool HidDev::Open(const HidColl&) { return false; }
bool HidDev::OpenReadOnly(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = true;
    return fakes.count(c.path) != 0;
}
bool HidDev::OpenFeatureOnly(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = false;
    return fakes.count(c.path) != 0;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
bool HidDev::Write(const std::vector<BYTE>&, int) { return false; }
void HidDev::Flush() {}
int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    FakeColl& f = fakes[c_.path];
    if (!f.endless.empty()) {
        buf = f.endless;
        return (int)buf.size();
    }
    if (f.inputs.empty() && f.lateAfterReads >= 0 && f.lateAfterReads-- == 0) f.inputs.push_back(f.late);
    if (f.inputs.empty()) {
        if (timeoutMs > 0) Sleep(timeoutMs);
        return -1;
    }
    buf = f.inputs.front();
    f.inputs.pop_front();
    return (int)buf.size();
}
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    // Only the documented reads may ever be sent, without any payload.
    CHECK(data.size() == 2 && data[0] == 0x09 && (data[1] == 0x8F || data[1] == 0x89));
    sent.push_back(data[1]);
    fakes[c_.path].last = data[1];
    return true;
}
bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    CHECK(id == 0x09);
    FakeColl& f = fakes[c_.path];
    auto it = f.replies.find(f.last);
    if (it == f.replies.end()) return false;
    out = it->second;
    return true;
}
std::vector<HidColl> EnumHid() { return {}; }
std::wstring HidProduct(const HidColl&) { return L""; }
std::wstring Hex4(USHORT v) {
    wchar_t s[8];
    swprintf(s, 8, L"%04X", v);
    return s;
}
int Clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

int main() {
    const HidColl rx = Coll(0x522C, L"rx");
    const HidColl cable = Coll(0x622C, L"cable");

    // 1. The user's mouse: receiver answers 89 with its frozen 58%, the mouse pushes 73%.
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, { kPush73, kPush73 } };
    auto ctx = Poll({ rx });
    const DeviceStatus* s = Find(ctx, L"incott:522C");
    CHECK(s && s->percent == 73 && !s->charging && s->connected && !s->asleep);
    CHECK(s->via == L"2.4 GHz receiver" && s->graceSec == 1800);
    CHECK(Count(0x89) == 0);  // the push was there: no status query at all (8F is the Wraith W1 check)

    // 2. Next poll, the mouse pushed 72% meanwhile: still no query.
    fakes[L"rx"].inputs = { kPush72 };
    ctx = Poll({ rx });
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->percent == 72 && Count(0x89) == 0);

    // 3. Nothing pushed since the last poll (mouse asleep): asleep, no percent, 89 not used.
    ctx = Poll({ rx });
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->asleep && s->percent == -1 && s->connected && Count(0x89) == 0);

    // 4. Receiver unplugged: the entry goes, the listener and the receiver kind are dropped.
    ctx = Poll({});
    CHECK(ctx.out.empty());

    // 5. Receiver plugged in again, nothing queued yet: a new listener waits for the next push.
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, {} };
    fakes[L"rx"].lateAfterReads = 5;
    fakes[L"rx"].late = kPush73;
    ctx = Poll({ rx });
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->percent == 73 && Count(0x89) == 0);
    ctx = Poll({});

    // 6. A new listener that gets no push within its wait: asleep rather than the receiver's 58%.
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, {} };
    ULONGLONG t0 = GetTickCount64();
    ctx = Poll({ rx });
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->asleep && s->percent == -1 && Count(0x89) == 0);
    CHECK(GetTickCount64() - t0 < 6000);
    ctx = Poll({});

    // 7. A receiver that is not an 8K one and pushes nothing keeps the 89 reading (unchanged behaviour).
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo1k }, { 0x89, kFeature58 } }, {} };
    ctx = Poll({ rx });
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->percent == 58 && !s->asleep && Count(0x89) == 1);
    ctx = Poll({});

    // 8. Receiver that never answers device info: no entry, and no 89 value taken on trust.
    fakes[L"rx"] = FakeColl{ { { 0x89, kFeature58 } }, {} };
    ctx = Poll({ rx });
    CHECK(!Find(ctx, L"incott:522C") && Count(0x89) == 0);
    ctx = Poll({});

    // 9. Cable plugged in while the receiver stays in: the cable answers 89 itself (97%) and the receiver,
    //    whose mouse is now on the cable and pushes nothing, gets no duplicate entry.
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, {} };
    fakes[L"cable"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kWired97 } }, {} };
    ctx = Poll({ rx, cable });
    s = Find(ctx, L"incott:622C");
    CHECK(s && s->percent == 97 && s->via == L"USB cable" && s->connected);
    CHECK(!Find(ctx, L"incott:522C"));

    // 10. Two mice, one on the cable and one still pushing through the receiver: both shown.
    fakes[L"rx"].inputs = { kPush73 };
    ctx = Poll({ rx, cable });
    CHECK(Find(ctx, L"incott:622C") && Find(ctx, L"incott:522C") && Find(ctx, L"incott:522C")->percent == 73);

    ctx = Poll({});

    // 11. A receiver that never stops sending valid pushes must not stall the poll (bounded polling).
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, {} };
    fakes[L"rx"].endless = kPush73;
    ctx = PollWithin({ rx }, 2000);
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->percent == 73 && Count(0x89) == 0);

    // 12. Same with an endless stream of reports that are not a status (other report id), on an existing
    //     listener and then on a new one waiting for its first push: bounded, asleep, 89 still not used.
    fakes[L"rx"].endless = Pad({ 0x08, 0x01, 0x02, 0x03 }, 33);
    ctx = PollWithin({ rx }, 2000);
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->asleep && s->percent == -1 && Count(0x89) == 0);
    ctx = Poll({});
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, {} };
    fakes[L"rx"].endless = Pad({ 0x08, 0x01, 0x02, 0x03 }, 33);
    ctx = PollWithin({ rx }, 6000);
    s = Find(ctx, L"incott:522C");
    CHECK(s && s->asleep && s->percent == -1 && Count(0x89) == 0);

    ctx = Poll({});

    // 13. Data cable plugged in, dongle still in (captured on the user's G23 Pro): the cable answers 89 with
    //     91% and no charging bit (byte 3 = 02 is the connection mode), while the mouse pushes 91% with the
    //     charging bit a moment later; the dongle pushes nothing. The first poll must wait for the push
    //     rather than show the feature reading, and must not show the dongle.
    const std::vector<BYTE> cableInfo = { 0x09, 0x8F, 0x01, 0x02, 0x00, 0xF1, 0x00, 0x00, 0x00 };
    const std::vector<BYTE> cableFeature91 = { 0x09, 0x89, 0x5B, 0x02, 0x00, 0x04, 0x00, 0x00, 0x5A };
    const std::vector<BYTE> cablePush91 = Pad({ 0x09, 0xDB, 0x40, 0x04, 0x00, 0x3F, 0x3F, 0x21 }, 33);
    fakes[L"rx"] = FakeColl{ { { 0x8F, kInfo8k }, { 0x89, kFeature58 } }, {} };
    fakes[L"cable"] = FakeColl{ { { 0x8F, cableInfo }, { 0x89, cableFeature91 } }, {} };
    fakes[L"cable"].lateAfterReads = 5;
    fakes[L"cable"].late = cablePush91;
    t0 = GetTickCount64();
    ctx = PollWithin({ rx, cable }, 6000);
    s = Find(ctx, L"incott:622C");
    CHECK(s && s->percent == 91 && s->charging && s->connected && s->via == L"USB cable");
    CHECK(!Find(ctx, L"incott:522C") && Count(0x89) == 0);
    CHECK(GetTickCount64() - t0 < 2500);  // the cable's push ends its wait; the dongle adds none
    // Next poll: the cable keeps pushing, the dongle stays silent.
    fakes[L"cable"].inputs = { cablePush91 };
    ctx = Poll({ rx, cable });
    CHECK(ctx.out.size() == 1 && Find(ctx, L"incott:622C") && Find(ctx, L"incott:622C")->charging);

    std::puts("incott_driver_test: all passed");
    return 0;
}
