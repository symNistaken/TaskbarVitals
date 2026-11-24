// Exercise the production Rongyuan driver against a simulated receiver; no hardware access.
// Checks the status parsing, the AJAZZ mouse model lookup and that only read commands are sent.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>

namespace {
// Simulated receiver state.
BYTE kbPercent = 0, msPercent = 100;
bool kbOnline = false, msOnline = true;
bool mouseAnswers = true;
UINT mouseId = 1755;  // AJ159 APEX, as read from a real mouse on the 3151:5007 dock
bool routedToMouse = false, infoPending = false, answerReady = false, fetchArmed = false;
int routes = 0, infoQueries = 0, opens = 0;

void Reset() {
    kbPercent = 0;
    msPercent = 100;
    kbOnline = false;
    msOnline = true;
    mouseAnswers = true;
    mouseId = 1755;
    routedToMouse = infoPending = answerReady = fetchArmed = false;
    routes = infoQueries = opens = 0;
}

HidColl Collection(USHORT vid = 0x3151, USHORT pid = 0x5007, const wchar_t* path = L"dock") {
    HidColl c;
    c.path = path;
    c.vid = vid;
    c.pid = pid;
    c.iface = 2;
    c.caps.UsagePage = 0xFFFF;
    c.caps.Usage = 0x0002;
    c.caps.FeatureReportByteLength = 65;
    return c;
}

PollContext Poll(const HidColl& c = Collection()) {
    PollContext ctx;
    ctx.colls = { c };
    PollRongyuan(ctx);
    return ctx;
}

// Forgets the cached ids, as when the receiver is unplugged.
void Unplug() {
    PollContext ctx;
    PollRongyuan(ctx);
}

bool AllZeroFrom(const std::vector<BYTE>& d, size_t from) {
    for (size_t i = from; i < d.size(); i++)
        if (d[i]) return false;
    return true;
}
}  // namespace

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    ++opens;
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }

bool HidDev::SetFeature(const std::vector<BYTE>& d) {
    assert(d.size() == 65 && d[0] == 0x00);
    switch (d[1]) {
    case 0xF7:  // status poll
        assert(AllZeroFrom(d, 2));
        fetchArmed = false;
        return true;
    case 0xF6:  // route to the mouse slot; never while a keyboard is paired and online
        assert(d[2] == 0x05 && AllZeroFrom(d, 3));
        assert(!kbOnline);
        routedToMouse = true;
        ++routes;
        return true;
    case 0x8F:  // GET_INFO with the vendor checksum
        assert(d[8] == 0x70 && AllZeroFrom(d, 9));
        for (int i = 2; i < 8; i++) assert(d[i] == 0);
        assert(routedToMouse);
        ++infoQueries;
        infoPending = mouseAnswers;
        answerReady = false;
        return true;
    case 0xFC:  // fetch the answer
        assert(AllZeroFrom(d, 2));
        fetchArmed = answerReady;
        return true;
    }
    assert(!"unexpected command");
    return false;
}

bool HidDev::GetFeature(BYTE id, std::vector<BYTE>& out) {
    assert(id == 0);
    out.assign(65, 0);
    if (fetchArmed) {
        out[1] = 0x8F;
        out[2] = mouseId & 0xFF;
        out[3] = (mouseId >> 8) & 0xFF;
        out[4] = (mouseId >> 16) & 0xFF;
        out[5] = mouseId >> 24;
        fetchArmed = answerReady = false;
        return true;
    }
    if (infoPending) {  // the mouse answers by the second status poll
        infoPending = false;
        answerReady = true;
    }
    out[1] = answerReady ? 1 : 0;
    out[2] = kbPercent;
    out[3] = msPercent;
    out[4] = kbOnline ? 0 : 1;
    out[5] = msOnline ? 0 : 1;
    out[6] = 1;  // ready to send
    out[7] = 2;
    return true;
}

std::wstring HidProduct(const HidColl&) { return L"AJAZZ 2.4G 8K"; }
std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int main() {
    // AJ159 APEX on its 8K dock: named from the mouse's device id, charge from the status frame.
    Reset();
    PollContext ctx = Poll();
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].name == L"AJAZZ AJ159 APEX" && ctx.out[0].percent == 100 && ctx.out[0].kind == DeviceKind::Mouse);
    assert(ctx.out[0].key == L"rongyuan:3151:5007:ms" && ctx.IsClaimed(0x3151, 0x5007));
    assert(routes == 1 && infoQueries == 1);

    // The id is read once per connection.
    msPercent = 64;
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"AJAZZ AJ159 APEX" && ctx.out[0].percent == 64);
    assert(infoQueries == 1);

    // Mouse off: nothing listed, and the id is asked again once it is back.
    msOnline = false;
    ctx = Poll();
    assert(ctx.out.empty() && infoQueries == 1);
    msOnline = true;
    mouseId = 3806;  // a different AJ159 APEX revision
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"AJAZZ AJ159 APEX" && infoQueries == 2);
    Unplug();

    // Other ids on the same shared PID keep the generic name.
    Reset();
    mouseId = 2014;  // another brand's mouse on 3151:5007
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"Wireless mouse" && ctx.out[0].percent == 100);
    Unplug();
    Reset();
    mouseId = 0xFFFFFFFF;
    ctx = Poll();
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"Wireless mouse");
    Unplug();

    // A mouse that never answers is asked on a few polls only, and still listed with its charge.
    Reset();
    mouseAnswers = false;
    for (int i = 0; i < 5; i++) {
        ctx = Poll();
        assert(ctx.out.size() == 1 && ctx.out[0].name == L"Wireless mouse" && ctx.out[0].percent == 100);
    }
    assert(infoQueries == 3);
    Unplug();

    // A keyboard on the same receiver: the receiver is never re-routed, both slots are listed.
    Reset();
    kbOnline = true;
    kbPercent = 80;
    ctx = Poll();
    assert(ctx.out.size() == 2 && routes == 0 && infoQueries == 0);
    // No id was read, so the receiver's brand ("AJAZZ 2.4G 8K") names both, as for other Rongyuan receivers.
    assert(ctx.out[0].name == L"AJAZZ wireless keyboard" && ctx.out[0].percent == 80);
    assert(ctx.out[1].name == L"AJAZZ wireless mouse" && ctx.out[1].percent == 100);
    Unplug();

    // Receivers of other vendors are only read, never re-routed.
    Reset();
    ctx = Poll(Collection(0x342D, 0xE3E0, L"other"));
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"AJAZZ wireless mouse" && routes == 0);  // stub product name
    Unplug();

    // Collections other than the vendor control one are not opened.
    Reset();
    HidColl wrongIface = Collection(), wrongUsage = Collection();
    wrongIface.iface = 1;
    wrongUsage.caps.Usage = 0x0001;
    ctx = {};
    ctx.colls = { wrongIface, wrongUsage };
    PollRongyuan(ctx);
    assert(ctx.out.empty() && opens == 0);

    puts("Rongyuan driver tests passed");
}
