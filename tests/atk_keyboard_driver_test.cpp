// Exercise the production poller against the Z87 receiver's battery report.
#include "../src/drivers/drivers.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>

namespace {
HidColl Receiver() {
    HidColl c;
    c.vid = 0x373B;
    c.pid = 0x1012;
    c.iface = 1;
    c.product = L"ATK Z87 Dongle";
    c.path = L"z87-receiver";
    c.caps.UsagePage = 0xFF1C;
    c.caps.Usage = 0x0092;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}

int opens = 0, z87Writes = 0;
BYTE percent = 92, charge = 0;
bool asleep = false;
std::deque<std::vector<BYTE>> pending;

PollContext Poll(std::vector<HidColl> colls) {
    PollContext ctx;
    ctx.colls = std::move(colls);
    opens = z87Writes = 0;
    PollAtkKeyboard(ctx);
    return ctx;
}
}  // namespace

std::wstring HidProduct(const HidColl& c) { return c.product; }
bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    return true;
}
void HidDev::Close() {}
void HidDev::Flush() { pending.clear(); }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    assert(data.size() >= 4 && data.size() <= c_.outLen());
    auto report = data;
    report.resize(c_.outLen(), 0);  // Win32 Write pads the caller's buffer.
    assert(report[0] == 4 && report[3] == 0x1A);
    assert(std::all_of(report.begin() + 5, report.end(), [](BYTE b) { return b == 0; }));
    if (c_.vid == 0x320F) {
        assert(report[4] == (c_.product.rfind(L"VGN", 0) == 0 ? 6 : 0));
        report[8] = 37;
        report[9] = 1;
    } else {
        ++z87Writes;
        if (asleep) {
            report[3] = 0xFF;  // receiver replied, keyboard did not
        } else {
            pending.emplace_back(report.begin(), report.begin() + 4);  // truncated reply
            auto stale = report;
            --stale[1];  // an older battery reply must not satisfy this request
            stale[8] = 13;
            pending.push_back(stale);
            report[8] = percent;
            report[9] = charge;
        }
    }
    pending.push_back(report);
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 1000);
    if (pending.empty()) return -1;
    out = std::move(pending.front());
    pending.pop_front();
    return (int)out.size();
}

int main() {
    auto z87 = Receiver();
    auto mouse = z87, wrongVendor = z87, otherInterface = z87, shortReport = z87, otherName = z87;
    mouse.pid = 0x1216;
    wrongVendor.vid = 0x320F;
    otherInterface.caps.UsagePage = 0xFFEF;
    shortReport.caps.OutputReportByteLength = 32;
    otherName.product = L"ATK Keyboard";
    auto ctx = Poll({mouse, wrongVendor, otherInterface, shortReport, otherName});
    assert(ctx.out.empty() && opens == 0);

    // An awake V75X on USB must not hide the Z87 receiver or supply its cached level.
    auto cable = z87;
    cable.vid = 0x320F;
    cable.pid = 0x5055;
    cable.product = L"ATK V75X";
    cable.path = L"v75x-cable";
    ctx = Poll({cable, z87, mouse});
    assert(opens == 2 && z87Writes == 1 && ctx.out.size() == 2);
    assert(ctx.IsClaimed(0x320F, 0x5055) && ctx.IsClaimed(0x320F, 0x5088));
    assert(ctx.IsClaimed(0x373B, 0x1012) && !ctx.IsClaimed(0x373B, 0x1216));
    for (const auto& s : ctx.out) {
        assert(s.kind == DeviceKind::Keyboard && !s.asleep);
        if (s.key == L"atk_keyboard:5088") {
            assert(s.percent == 37 && s.charging && s.via == L"USB cable");
        } else {
            assert(s.key == L"atk_keyboard:1012" && s.name == L"ATK Z87");
            assert(s.percent == 92 && !s.charging && s.via == L"2.4 GHz receiver");
        }
    }
    ctx = Poll({z87});
    assert(z87Writes == 0 && ctx.out.size() == 1 && ctx.out.front().percent == 92);

    // An unplug invalidates the cached 92%. An asleep receiver returns FF on the first read.
    Poll({});
    asleep = true;
    ctx = Poll({z87});
    assert(z87Writes == 1 && ctx.out.size() == 1 && ctx.IsClaimed(z87.vid, z87.pid));
    assert(ctx.out.front().asleep && ctx.out.front().percent < 0);
    ctx = Poll({z87});
    assert(z87Writes == 0 && ctx.out.front().asleep && ctx.out.front().percent < 0);

    // A valid header can still carry invalid data. Genuine zero is an accepted battery level.
    asleep = false;
    for (auto [level, state] : {std::pair<int, int>{101, 0}, {92, 3}, {0, 2}}) {
        Poll({});
        percent = (BYTE)level;
        charge = (BYTE)state;
        ctx = Poll({z87});
        assert(z87Writes == 1 && ctx.out.size() == 1);
        const auto& s = ctx.out.front();
        if (level == 0) assert(!s.asleep && s.percent == 0 && s.charging);
        else assert(s.asleep && s.percent < 0 && !s.charging);
    }

    // Preserve the newer VGN and generic Feker paths when adding Z87.
    cable.product=L"VGN S99";
    ctx=Poll({cable});
    assert(ctx.out.size()==1 && ctx.out[0].name==L"VGN S99" && ctx.out[0].percent==37);
    cable.product=L"Wireless Gaming Keyboard";
    ctx=Poll({cable});
    assert(ctx.out.size()==1 && ctx.out[0].name==L"Wireless Gaming Keyboard" && ctx.out[0].percent==37);
    std::puts("ATK Z87 and existing keyboard regression tests passed");
}
