// Production pollers with simulated HID replies; no hardware is accessed.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>

static std::vector<BYTE> reply;
static std::vector<BYTE> inputReport;
static int opens;
static int ajazzCommandIndex;
static bool openOk = true;

bool HidDev::Open(const HidColl& c) {
    ++opens;
    c_ = c;
    if (c.pid == 0x6425) ajazzCommandIndex = 0;
    return openOk;
}
void HidDev::Close() {}
void HidDev::Flush() {}
int HidDev::Read(std::vector<BYTE>& r, int) {
    if (inputReport.empty()) return -1;
    r = inputReport;
    inputReport.clear();
    return (int)r.size();
}
int HidDev::SetOutputAndRead(const std::vector<BYTE>& q, std::vector<BYTE>& r, int ms) {
    if (c_.pid == 0x6434) {
        std::vector<BYTE> expected(64);
        expected[0] = 6;
        expected[1] = 5;
        assert(q == expected && ms == 500);
        r = reply;
        return r.empty() ? -1 : (int)r.size();
    }

    static const std::vector<std::vector<BYTE>> commands = {
        { 0x06, 0x05 }, { 0x06, 0x05 }, { 0x06, 0x07 },
        { 0x06, 0x11 }, { 0x06, 0x31, 0x3B }, { 0x06, 0x31, 0x05, 0x3B },
    };
    assert(c_.pid == 0x6425);
    assert(ajazzCommandIndex < (int)commands.size());
    assert(q == commands[ajazzCommandIndex]);
    ++ajazzCommandIndex;
    assert(ms == 250);
    r.assign(64, 0);
    r[0] = 0x06;
    r[1] = commands[ajazzCommandIndex - 1][1];  // ordinary command echo, not battery status
    return (int)r.size();
}

static HidColl Collection(USHORT pid = 0x6434) {
    HidColl c;
    c.path = pid == 0x6425 ? L"AJAZZ_TEST_COLLECTION" : L"ICARUS_TEST_COLLECTION";
    c.vid = 0x7768;
    c.pid = pid;
    c.iface = 1;
    c.caps.UsagePage = 0xFFB2;
    c.caps.Usage = 1;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 64;
    return c;
}

static std::vector<BYTE> AjazzStatus(int percent, int charging) {
    std::vector<BYTE> r(64, 0);
    r[0] = 0x06;
    r[1] = 0xFA;
    r[2] = 0x03;
    r[5] = 0x03;
    r[6] = (BYTE)percent;
    r[7] = (BYTE)charging;
    return r;
}

int main() {
    // Existing ICARUS V2 protocol remains unchanged.
    for (int level : { 0, 90, 100, 101 }) {
        PollContext c;
        c.colls = { Collection() };
        reply.assign(64, 0);
        reply[0] = 6;
        reply[1] = 5;
        reply[2] = 0x26;
        reply[13] = (BYTE)level;
        PollPressPlayIcarus(c);
        assert(c.out.size() == (level <= 100 ? 1u : 0u));
        if (level <= 100) {
            assert(c.out[0].percent == level);
            int before = opens;
            PollPressPlayIcarus(c);
            assert(opens == before);
        }
    }

    // AJAZZ: the six captured read-only output reports are sent, then 06 FA is decoded.
    {
        PollContext c;
        c.colls = { Collection(0x6425) };
        inputReport = AjazzStatus(100, 1);
        opens = 0;
        PollPressPlayIcarus(c);
        assert(ajazzCommandIndex == 6);
        assert(c.out.size() == 1);
        assert(c.out[0].key == L"ajazz:aj159nlp-nearlink");
        assert(c.out[0].percent == 100);
        assert(c.out[0].charging);
    }

    // A cached AJAZZ reading remains available on the next refresh if no new status arrives.
    {
        PollContext c;
        c.colls = { Collection(0x6425) };
        inputReport.clear();
        PollPressPlayIcarus(c);
        assert(ajazzCommandIndex == 6);  // do not replay startup commands after a valid status
        assert(c.out.size() == 1);
        assert(c.out[0].percent == 100 && c.out[0].charging);
    }

    // Reject malformed status frames and out-of-range values.
    for (int fault = 0; fault < 4; ++fault) {
        PollContext c;
        HidColl h = Collection(0x6425);
        h.path = L"AJAZZ_INVALID_" + std::to_wstring(fault);
        c.colls = { h };
        inputReport = AjazzStatus(75, 0);
        if (fault == 0) inputReport[2] ^= 1;
        if (fault == 1) inputReport[5] ^= 1;
        if (fault == 2) inputReport[6] = 101;
        if (fault == 3) inputReport[7] = 2;
        PollPressPlayIcarus(c);
        assert(c.out.empty());
    }

    // Ignore unrelated HID collections.
    for (int fault = 0; fault < 5; ++fault) {
        HidColl h = Collection(0x6425);
        h.path = L"AJAZZ_WRONG_COLLECTION_" + std::to_wstring(fault);
        if (fault == 0) h.iface = 0;
        if (fault == 1) h.vid++;
        if (fault == 2) h.pid++;
        if (fault == 3) h.caps.UsagePage++;
        if (fault == 4) h.caps.Usage++;
        PollContext c;
        c.colls = { h };
        opens = 0;
        PollPressPlayIcarus(c);
        assert(opens == 0 && c.out.empty());
    }

    openOk = false;
    PollContext c;
    c.colls = { Collection() };
    PollPressPlayIcarus(c);
    assert(c.out.empty());
    puts("Press Play / AJAZZ tests passed");
}
