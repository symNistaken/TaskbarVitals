#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {

struct Dongle {
    bool awake = true;
    BYTE percent = 60;
    BYTE stuck = 0;
    std::vector<BYTE> reply = std::vector<BYTE>(1041, 0);
    std::string sent;
} dongle;

void Answer(BYTE cmd, BYTE target) {
    auto& r = dongle.reply;
    r[0] = 0xA1;
    if (dongle.stuck && !(cmd == 0x0F && target == 0x0F)) { r[1] = dongle.stuck; return; }
    BYTE st = 0x01;
    if (cmd == 0x0F && target == 0x0F) {
        dongle.stuck = 0;
    } else if (cmd == 0x0F) {
        st = dongle.awake ? 0x01 : 0x03;
    } else if (cmd == 0xB4) {
        if (dongle.awake) { r[16] = dongle.percent; r[17] = 0x09; r[18] = 0x0F; } else st = 0x08;
    } else if (cmd == 0x0E) {
        BYTE info[] = { 0x67, 0x33, 0x84, 0x19, 0x83, 0x19, 0x01, 0x07 };
        if (dongle.awake) std::copy(info, info + 8, r.begin() + 16); else st = 0x11;
    } else {
        assert(!"only the read commands B4, 0E and 0F may be sent");
    }
    if (st == 0x08 || st == 0x11) dongle.stuck = st;
    r[1] = st;
}

HidColl Config(USHORT page = 0xFF01, USHORT usage = 0x0002) {
    HidColl c;
    c.path = L"endgame-config";
    c.vid = 0x3367;
    c.pid = 0x1970;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.FeatureReportByteLength = 1041;
    return c;
}

PollContext Poll(const HidColl& c = Config()) {
    dongle.sent.clear();
    PollContext ctx;
    ctx.colls = { c };
    PollEndgame(ctx);
    return ctx;
}

}

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    assert(c.path == L"endgame-config");
    c_ = c;
    return true;
}
void HidDev::Close() {}
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    assert(data.size() >= 3 && data[0] == 0xA1);
    char text[8];
    snprintf(text, sizeof text, "%02X:%02X ", data[1], data[2]);
    dongle.sent += text;
    Answer(data[1], data[2]);
    return true;
}
bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    assert(reportId == 0xA1);
    out = dongle.reply;
    return true;
}
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    dongle = Dongle{};
    auto ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.IsClaimed(0x3367, 0x1970));
    assert(ctx.out[0].percent == 60 && !ctx.out[0].asleep);
    assert(ctx.out[0].name == L"Endgame Gear OP1w 4k v2" && ctx.out[0].key == L"endgame:1970");
    assert(dongle.sent == "B4:00 0E:00 ");

    dongle.awake = false;
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].asleep && ctx.out[0].name == L"Endgame Gear OP1w 4k v2");

    dongle.awake = true;
    dongle.percent = 55;
    assert(dongle.stuck);
    ctx = Poll();
    assert(ctx.out.size() == 1 && !ctx.out[0].asleep && ctx.out[0].percent == 55);
    assert(dongle.sent == "B4:00 0F:0F B4:00 0E:00 ");

    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 55 && dongle.sent == "B4:00 0E:00 ");

    ctx = Poll(Config(0xFF02, 0x0002));
    assert(ctx.out.empty() && dongle.sent.empty());

    puts("Endgame driver tests passed");
}
