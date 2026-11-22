// Run with MinGW-w64 from the repository root (with build/ already created):
// g++ -std=c++17 tests/aula_headset_driver_test.cpp src/drivers/aula_headset.cpp -o build/aula_headset_driver_test.exe
// ./build/aula_headset_driver_test.exe
#include "../src/drivers/drivers.h"

#include <cassert>

namespace {

int opens = 0;
std::vector<std::vector<BYTE>> replies;

HidColl Collection() {
    HidColl c;
    c.vid = 0x38A6;
    c.pid = 0x0001;
    c.iface = 0;
    c.path = L"AULA-A7Pro";
    c.caps.UsagePage = 0xFF90;
    c.caps.Usage = 1;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}

std::vector<BYTE> Battery(BYTE percent, BYTE powerState = 0x02) {
    std::vector<BYTE> r = {0x55, 0x65, percent, powerState};
    r.resize(64);
    return r;
}

}  // namespace

std::wstring HidProduct(const HidColl& c) { return c.path; }
bool HidDev::Open(const HidColl&) { ++opens; return true; }
void HidDev::Close() {}
void HidDev::Flush() {}
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    assert((data == std::vector<BYTE>{0x55, 0x65, 0x01}));
    return true;
}
int HidDev::Read(std::vector<BYTE>& data, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 750);
    if (replies.empty()) return -1;
    data = replies.front();
    replies.erase(replies.begin());
    return static_cast<int>(data.size());
}

int main() {
    auto otherModel = Collection();
    otherModel.path = L"AULA-A700";
    auto otherCollection = Collection();
    otherCollection.caps.UsagePage = 0xFF82;
    PollContext ctx;
    ctx.colls = {otherModel, otherCollection};
    PollAulaHeadset(ctx);
    assert(opens == 0 && ctx.out.empty() && ctx.claimed.empty());

    ctx.colls.push_back(Collection());
    auto unrelated = Battery(42);
    unrelated[1] = 0x64;
    replies = {{0x55, 0x65, 50}, std::move(unrelated), Battery(0xFF)};
    PollAulaHeadset(ctx);
    assert(opens == 1 && ctx.out.empty() && ctx.claimed.empty());

    struct Scenario {
        BYTE percent;
        BYTE powerState;
        bool charging;
        bool full;
    };
    for (const Scenario scenario : {Scenario{0, 0x02, false, false},
                                    Scenario{37, 0x02, false, false},
                                    Scenario{100, 0x02, false, false},
                                    Scenario{0, 0x00, true, false},
                                    Scenario{37, 0x00, true, false},
                                    Scenario{90, 0x00, true, false},
                                    Scenario{100, 0x00, false, true}}) {
        PollContext current;
        current.colls = ctx.colls;
        replies = {Battery(0xFF), Battery(scenario.percent, scenario.powerState)};
        PollAulaHeadset(current);
        assert(current.out.size() == 1 && current.IsClaimed(0x38A6, 0x0001));
        const auto& s = current.out.front();
        assert(s.percent == scenario.percent && s.kind == DeviceKind::Headset);
        assert(s.charging == scenario.charging && s.full == scenario.full);
        assert(s.key == L"aula:a7pro" && s.via == L"2.4 GHz receiver");
    }

    PollContext malformed;
    malformed.colls = ctx.colls;
    replies = {Battery(90, 0x01)};
    PollAulaHeadset(malformed);
    assert(malformed.out.empty());
}
