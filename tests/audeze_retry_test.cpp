// Test the production Maxwell reader with scripted HID responses.
#include "../src/drivers/drivers.h"
#include <cassert>
#include <cstdio>
#include <deque>
static std::deque<std::vector<BYTE>> frames;
static int writes, reads, opens;
static bool writeOk = true;
static std::wstring product = L"Audeze Maxwell";
bool HidDev::Open(const HidColl& c) { c_ = c; ++opens; return true; }
void HidDev::Close() {}
bool HidDev::Write(const std::vector<BYTE>& q, int ms) {
    std::vector<BYTE> expected(62, 0);
    const BYTE prefix[] = {6, 7, 0x80, 5, 0x5A, 3, 0, 0xD6, 0x0C};
    std::copy(std::begin(prefix), std::end(prefix), expected.begin());
    assert(q == expected && ms == 500); ++writes; return writeOk;
}
bool HidDev::GetInputReport(BYTE id, std::vector<BYTE>& r) {
    assert(id == 7); ++reads;
    if (frames.empty()) return false;
    r = frames.front(); frames.pop_front(); return !r.empty();
}
std::wstring HidProduct(const HidColl&) { return product; }
#include "../src/drivers/audeze.cpp"
static std::vector<BYTE> Level(BYTE n) { return {7, 8, 0x80, 0xD6, 0x0C, 0, 0, n}; }
static HidColl Collection(USHORT pid = 0x4B29) {
    HidColl c; c.vid = 0x3329; c.pid = pid;
    c.caps.UsagePage = 0xFF13; c.caps.Usage = 1;
    c.caps.InputReportByteLength = c.caps.OutputReportByteLength = 62; return c;
}
static void SetFrames(std::deque<std::vector<BYTE>> f) { frames = f; writes = reads = opens = 0; }
int main() {
    SetFrames({Level(80), Level(81), Level(82)});
    assert(ReadLevel(Collection()) == 82 && writes == 1 && reads == 3);
    SetFrames({{}, {}, {}, Level(73)});
    assert(ReadLevel(Collection()) == 73 && writes == 1 && reads == 4);
    SetFrames({{}, {}, {}, {}, Level(65), {}, {}});
    int retried = ReadLevel(Collection());
    assert(retried == 65 && writes == 2 && reads == 8);
    SetFrames({{7}, {7, 0xD6, 0x0C, 0, 0, 101}});
    assert(ReadLevel(Collection()) == -1 && writes == 2 && reads == 8);
    for (BYTE level : {BYTE(0), BYTE(100)}) {
        SetFrames({Level(level), Level(level), Level(level)});
        assert(ReadLevel(Collection()) == level);
    }
    writeOk = false; SetFrames({});
    assert(ReadLevel(Collection()) == -1 && writes == 1 && reads == 0);
    writeOk = true;
    for (USHORT pid : {USHORT(0x4B29), USHORT(0x4B28), USHORT(0x4B19), USHORT(0x4B1A)}) {
        SetFrames({Level(86), Level(86), Level(86)});
        PollContext ctx; ctx.colls = {Collection(pid)}; PollAudeze(ctx);
        assert(ctx.out.size() == 1 && ctx.out[0].percent == 86);
        assert(ctx.out[0].name == ((pid == 0x4B29 || pid == 0x4B28) ? L"Audeze Maxwell 2" : L"Audeze Maxwell"));
        assert(ctx.out[0].charging == (pid == 0x4B1A));
        PollAudeze(ctx); assert(opens == 1);
    }
    for (int fault = 0; fault < 3; ++fault) {
        SetFrames({Level(86)}); auto c = Collection();
        if (fault == 0) c.pid = 0xFFFF;
        if (fault == 1) ++c.caps.UsagePage;
        if (fault == 2) ++c.caps.Usage;
        PollContext ctx; ctx.colls = {c}; PollAudeze(ctx); assert(opens == 0 && ctx.out.empty());
    }
    product = L"Audeze Maxwell 2 Dongle"; SetFrames({Level(86)});
    PollContext ctx; ctx.colls = {Collection()}; PollAudeze(ctx);
    assert(opens == 0 && ctx.out.empty());
    puts("Maxwell retry and identity tests passed");
}
