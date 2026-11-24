// Exercise the Keychron mouse driver with a simulated HID transport; no hardware access.
#include "../src/drivers/drivers.h"

#include <cassert>
#include <cstdio>
#include <map>

namespace {
// Scripted device: a reply per request (command byte, sub byte), queued when the request is written.
std::map<std::pair<BYTE, BYTE>, std::vector<BYTE>> script;
std::vector<std::vector<BYTE>> pending;
std::vector<std::vector<BYTE>> writes;
int opens = 0;

HidColl Collection(USHORT pid, USHORT page, USHORT usage) {
    HidColl c;
    c.path = L"keychron";
    c.vid = 0x3434;
    c.pid = pid;
    c.caps.UsagePage = page;
    c.caps.Usage = usage;
    c.caps.InputReportByteLength = 65;
    c.caps.OutputReportByteLength = 65;
    return c;
}

// 64-byte input report as Read() hands it to the driver (report id 0 already dropped).
std::vector<BYTE> Reply(BYTE cmd, BYTE sub, std::initializer_list<std::pair<int, BYTE>> bytes) {
    std::vector<BYTE> r(64, 0);
    r[0] = cmd;
    r[2] = 0x81;
    r[3] = sub;
    for (auto [i, v] : bytes) r[i] = v;
    return r;
}

// The exact output report the driver must send: report id 0, [cmd][00][81][sub], zeros, checksum.
std::vector<BYTE> Request(BYTE cmd, BYTE sub, BYTE checksum) {
    std::vector<BYTE> w(65, 0);
    w[1] = cmd;
    w[3] = 0x81;
    w[4] = sub;
    w[64] = checksum;
    return w;
}
const std::vector<BYTE> kAsk01 = Request(0x01, 0x01, 0x1E);  // 0xA1 - (01 + 81 + 01)
const std::vector<BYTE> kAsk41 = Request(0x41, 0x01, 0xDE);  // 0xA1 - (41 + 81 + 01)
const std::vector<BYTE> kAsk40 = Request(0x40, 0x00, 0xE0);  // 0xA1 - (40 + 81 + 00)

PollContext Poll(const HidColl& c, std::map<std::pair<BYTE, BYTE>, std::vector<BYTE>> replies) {
    script = std::move(replies);
    pending.clear();
    writes.clear();
    PollContext ctx;
    ctx.colls = { c };
    PollKeychron(ctx);
    return ctx;
}

// 1K receiver (D030/D031): feature report 0x51 on the vendor collection of usage page 0x8C.
std::vector<BYTE> featureReply;               // what GetFeature returns after a matching SetFeature
std::vector<std::vector<BYTE>> featureWrites;
bool featureQueued = false;

HidColl LegacyCollection(USHORT pid) {
    HidColl c;
    c.path = L"keychron-1k";
    c.vid = 0x3434;
    c.pid = pid;
    c.caps.UsagePage = 0x8C;
    c.caps.Usage = 0x01;
    c.caps.FeatureReportByteLength = 1001;
    return c;
}

// Status reply [51][06][ok][00][vid lo][vid hi][pid lo][pid hi][..][..][..][battery %][charging].
std::vector<BYTE> Status(BYTE ok, USHORT vid, USHORT pid, BYTE battery, BYTE charging) {
    return { 0x51, 0x06, ok, 0x00, (BYTE)vid, (BYTE)(vid >> 8), (BYTE)pid, (BYTE)(pid >> 8), 0, 0, 0, battery, charging };
}

PollContext PollReceiver1k(const HidColl& c, std::vector<BYTE> reply) {
    featureReply = std::move(reply);
    featureWrites.clear();
    featureQueued = false;
    return Poll(c, {});
}
// Ultra-Link 8K receiver (D028): vendor collection FFC1:0001 with numbered 64-byte reports.
HidColl UltraCollection(USHORT page = 0xFFC1) {
    HidColl c;
    c.path = L"keychron-ultra";
    c.vid = 0x3434;
    c.pid = 0xD028;
    c.caps.UsagePage = page;
    c.caps.Usage = 0x01;
    c.caps.InputReportByteLength = 64;
    c.caps.OutputReportByteLength = 64;
    return c;
}

// Input report as Read() hands it over: [report id][cmd][payload...], 64 bytes; `bytes` index the
// payload as the Launcher sees it (cmd at 0).
std::vector<BYTE> UltraReply(BYTE id, BYTE cmd, std::initializer_list<std::pair<int, BYTE>> bytes) {
    std::vector<BYTE> r(64, 0);
    r[0] = id;
    r[1] = cmd;
    for (auto [i, v] : bytes) r[i + 1] = v;
    return r;
}
// Receiver slots: [03] then per slot vid LE16, pid LE16, status at payload offsets 2/7/12.
std::vector<BYTE> UltraSlots(int slot, BYTE status) {
    int off = 2 + slot * 5;
    return UltraReply(0xB5, 0x03, { { off, 0x34 }, { off + 1, 0x34 }, { off + 2, 0x49 }, { off + 3, 0xD0 }, { off + 4, status } });
}
const std::vector<BYTE> kUltraAskSlots = { 0xB5, 0x03 };
const std::vector<BYTE> kUltraAskBase = { 0xB3, 0x06 };

// Raw HID collection FF60:0061 (unnumbered 32-byte reports) of a receiver or of a keyboard on its cable.
HidColl RawCollection(USHORT pid) {
    HidColl c;
    c.path = L"keychron-raw";
    c.vid = 0x3434;
    c.pid = pid;
    c.caps.UsagePage = 0xFF60;
    c.caps.Usage = 0x61;
    c.caps.InputReportByteLength = 33;
    c.caps.OutputReportByteLength = 33;
    return c;
}
// 32-byte input report as Read() hands it over; the script key is its first two request bytes.
std::vector<BYTE> RawReply(std::initializer_list<BYTE> bytes) {
    std::vector<BYTE> r(bytes);
    r.resize(32, 0);
    return r;
}
// The exact output report the driver must send: report id 0, the command bytes, zeros.
std::vector<BYTE> RawRequest(std::initializer_list<BYTE> bytes) {
    std::vector<BYTE> w(1, 0);
    w.insert(w.end(), bytes.begin(), bytes.end());
    w.resize(33, 0);
    return w;
}
const std::vector<BYTE> kRawAskSlots = RawRequest({ 0xB2 });
const std::vector<BYTE> kRawAskUltra = RawRequest({ 0xA7, 0x31 });  // Ultra 8K (ZMK) keyboards
const std::vector<BYTE> kRawAskQmk = RawRequest({ 0xA4 });          // QMK keyboards with KC_GET_BATTERY

PollContext PollAll(std::vector<HidColl> colls, std::map<std::pair<BYTE, BYTE>, std::vector<BYTE>> replies) {
    script = std::move(replies);
    pending.clear();
    writes.clear();
    PollContext ctx;
    ctx.colls = std::move(colls);
    PollKeychron(ctx);
    return ctx;
}
}  // namespace

// Stand-ins for the Win32 HID transport. The production driver is linked unchanged.
bool HidDev::Open(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    overlapped_ = true;
    ++opens;
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
void HidDev::Flush() { pending.clear(); }
bool HidDev::Write(const std::vector<BYTE>& data, int) {
    writes.push_back(data);
    if (c_.page() == 0xFFC1) {  // Ultra-Link 8K: numbered report [id][cmd], keyed by (id, cmd)
        assert(data.size() == 2 && (data[0] == 0xB5 || data[0] == 0xB3));
        auto it = script.find({ data[0], data[1] });
        if (it != script.end()) pending.push_back(it->second);
        return true;
    }
    if (c_.page() == 0xFF60) {  // raw HID: report id 0 + 32 bytes, keyed by the first two request bytes
        assert(data.size() == 33 && data[0] == 0);
        auto it = script.find({ data[1], data[2] });
        if (it != script.end()) pending.push_back(it->second);
        return true;
    }
    assert(data.size() == 65 && data[0] == 0);
    auto it = script.find({ data[1], data[4] });
    if (it != script.end()) pending.push_back(it->second);
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int timeoutMs) {
    assert(timeoutMs > 0 && timeoutMs <= 600);
    if (pending.empty()) return -1;
    out = pending.front();
    pending.erase(pending.begin());
    return (int)out.size();
}
bool HidDev::OpenFeatureOnly(const HidColl& c) { return Open(c); }
bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    featureWrites.push_back(data);
    featureQueued = data.size() >= 2 && data[0] == 0x51 && data[1] == 0x06 && !featureReply.empty();
    return true;
}
bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    assert(reportId == 0x51);
    out.assign(c_.featLen(), 0);
    out[0] = reportId;
    if (!featureQueued) return true;  // nothing queued yet: the report reads back empty
    std::copy(featureReply.begin(), featureReply.end(), out.begin());
    return true;
}
std::wstring HidProduct(const HidColl&) { return L"Keychron 4K Link"; }
std::wstring Hex4(USHORT v) {
    wchar_t text[8];
    swprintf(text, 8, L"%04X", v);
    return text;
}

int main() {
    const auto link4k = Collection(0xD038, 0xFF0A, 0x01);

    // 4K Link: 01 reply without 34 34 / 2D 36 at bytes 6/7 selects the "4k" layout. Battery at byte 6,
    // state at byte 5 (non-zero = charging), paired mouse's product id at bytes 4/5 of the 40 reply.
    auto ctx = Poll(link4k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, { { 6, 0x12 }, { 7, 0x05 } }) },
                              { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 5, 1 }, { 6, 73 }, { 7, 2 }, { 10, 0 }, { 11, 0 } }) },
                              { { 0x40, 0x00 }, Reply(0x40, 0x00, { { 4, 0x21 }, { 5, 0x06 } }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kAsk01, kAsk41, kAsk40 }));
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == L"keychron:D038" && ctx.out[0].name == L"Keychron M4 4K");
    assert(ctx.out[0].percent == 73 && ctx.out[0].charging);
    assert(ctx.out[0].kind == DeviceKind::Mouse && ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.IsClaimed(0x3434, 0xD038));

    // State 0 = not charging; any other state counts as charging. The last case gets no 40 reply.
    ctx = Poll(link4k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, {}) },
                         { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 5, 0 }, { 6, 100 } }) },
                         { { 0x40, 0x00 }, Reply(0x40, 0x00, { { 4, 0xA0 }, { 5, 0x07 } }) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && !ctx.out[0].charging);
    assert(ctx.out[0].name == L"Keychron M3 4K");
    ctx = Poll(link4k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, {}) },
                         { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 5, 2 }, { 6, 40 } }) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 40 && ctx.out[0].charging);

    // Without a 40 reply, or with an unknown product id, the receiver name is not used and no model
    // is invented.
    assert((writes == std::vector<std::vector<BYTE>>{ kAsk01, kAsk41, kAsk40 }));
    assert(ctx.out[0].name == L"Keychron mouse");
    ctx = Poll(link4k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, {}) },
                         { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 6, 55 } }) },
                         { { 0x40, 0x00 }, Reply(0x40, 0x00, { { 4, 0x99 }, { 5, 0x09 } }) } });
    assert(ctx.out.size() == 1 && ctx.out[0].name == L"Keychron mouse" && ctx.out[0].percent == 55);

    // Battery 0 or out of range on the 4K path: nothing listed, nothing claimed, no 40 query.
    for (BYTE bad : { (BYTE)0, (BYTE)101, (BYTE)0xFF }) {
        ctx = Poll(link4k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, {}) },
                             { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 5, 1 }, { 6, bad }, { 10, 1 }, { 11, 50 } }) } });
        assert(ctx.out.empty() && ctx.claimed.empty());
        assert((writes == std::vector<std::vector<BYTE>>{ kAsk01, kAsk41 }));
    }

    // No reply at all (mouse asleep / unpaired): nothing listed or claimed.
    ctx = Poll(link4k, {});
    assert(ctx.out.empty() && ctx.claimed.empty());
    assert((writes == std::vector<std::vector<BYTE>>{ kAsk01, kAsk41 }));
    // 01 answered but 41 not: still nothing.
    ctx = Poll(link4k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, {}) } });
    assert(ctx.out.empty() && ctx.claimed.empty());
    assert((writes == std::vector<std::vector<BYTE>>{ kAsk01, kAsk41 }));

    // 8K Nordic tag 34 34 at bytes 6/7 on the same collection: unchanged classic path, battery at
    // byte 11, charging at byte 10, only the 01 query is sent.
    const auto receiver8k = Collection(0xD028, 0xFF0A, 0x01);
    ctx = Poll(receiver8k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, { { 5, 0 }, { 6, 0x34 }, { 7, 0x34 }, { 10, 1 }, { 11, 64 } }) },
                             { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 6, 99 } }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kAsk01 }));
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"keychron:D028");
    assert(ctx.out[0].percent == 64 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"Keychron mouse" && ctx.out[0].via == L"2.4 GHz receiver");

    // 8K Nordic tag 2D 36: the 8K receiver layout of the 41 reply (state byte 10, battery byte 11),
    // not the 4K one (byte 6 is ignored).
    ctx = Poll(receiver8k, { { { 0x01, 0x01 }, Reply(0x01, 0x01, { { 6, 0x2D }, { 7, 0x36 } }) },
                             { { 0x41, 0x01 }, Reply(0x41, 0x01, { { 5, 1 }, { 6, 99 }, { 10, 3 }, { 11, 81 } }) },
                             { { 0x40, 0x00 }, Reply(0x40, 0x00, { { 4, 0x21 }, { 5, 0x06 } }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kAsk01, kAsk41 }));
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 81 && !ctx.out[0].charging);
    assert(ctx.out[0].name == L"Keychron mouse");

    // Collections that are not 65/65 vendor-page ones are never opened.
    opens = 0;
    auto shortReport = link4k;
    shortReport.caps.InputReportByteLength = 33;
    auto mousePage = link4k;
    mousePage.caps.UsagePage = 0x01;
    for (const auto& c : { shortReport, mousePage }) {
        ctx = Poll(c, { { { 0x01, 0x01 }, Reply(0x01, 0x01, {}) } });
        assert(ctx.out.empty() && writes.empty());
    }
    assert(opens == 0);

    // 1K receiver (D030): M3 (D033) at 87 %, not charging. Only the status command is sent, the
    // receiver is claimed and the 65-byte protocol is not tried on it.
    const auto legacyA = LegacyCollection(0xD030);
    ctx = PollReceiver1k(legacyA, Status(0x01, 0x3434, 0xD033, 87, 0x00));
    assert((featureWrites == std::vector<std::vector<BYTE>>{ { 0x51, 0x06 } }));
    assert(writes.empty());
    assert(ctx.out.size() == 1);
    assert(ctx.out[0].key == L"keychron:D033" && ctx.out[0].name == L"Keychron M3");
    assert(ctx.out[0].percent == 87 && !ctx.out[0].charging);
    assert(ctx.out[0].kind == DeviceKind::Mouse && ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.IsClaimed(0x3434, 0xD030));

    // Type-C receiver (D031), charging; an unknown mouse pid gets the generic name.
    ctx = PollReceiver1k(LegacyCollection(0xD031), Status(0x01, 0x3434, 0xD0FF, 100, 0x01));
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 100 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"Keychron mouse" && ctx.IsClaimed(0x3434, 0xD031));

    // Error status, foreign vendor id, battery out of range, or no reply: nothing listed or claimed.
    for (const auto& bad : { Status(0x00, 0x3434, 0xD033, 50, 0), Status(0x01, 0x1234, 0xD033, 50, 0),
                             Status(0x01, 0x3434, 0xD033, 101, 0), std::vector<BYTE>{} }) {
        ctx = PollReceiver1k(legacyA, bad);
        assert(ctx.out.empty() && ctx.claimed.empty());
    }

    // The same usage page without a feature report, or another receiver pid: never opened.
    opens = 0;
    auto noFeature = legacyA;
    noFeature.caps.FeatureReportByteLength = 0;
    auto otherPid = LegacyCollection(0xD038);
    for (const auto& c : { noFeature, otherPid }) {
        ctx = PollReceiver1k(c, Status(0x01, 0x3434, 0xD033, 87, 0));
        assert(ctx.out.empty() && featureWrites.empty());
    }
    assert(opens == 0);

    // Ultra-Link 8K (D028): slot query B5 03, then base info B3 06; payload byte 19 = power
    // (bits 0-6 battery %, bit 7 charging). Mouse in the second slot, 0xE4 = charging at 100 %.
    const auto ultra = UltraCollection();
    ctx = Poll(ultra, { { { 0xB5, 0x03 }, UltraSlots(1, 1) },
                        { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 0xE4 } }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots, kUltraAskBase }));
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"keychron:D028");
    assert(ctx.out[0].percent == 100 && ctx.out[0].charging);
    assert(ctx.out[0].name == L"Keychron mouse" && ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.out[0].kind == DeviceKind::Mouse && ctx.IsClaimed(0x3434, 0xD028));
    // 0x37 = 55 %, not charging; mouse in the first slot.
    ctx = Poll(ultra, { { { 0xB5, 0x03 }, UltraSlots(0, 1) },
                        { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 0x37 } }) } });
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 55 && !ctx.out[0].charging);

    // No connected slot, no slot reply, no base reply or power 0 / out of range: nothing listed,
    // nothing claimed; B3 06 is only sent when a slot is connected.
    ctx = Poll(ultra, { { { 0xB5, 0x03 }, UltraSlots(0, 0) },
                        { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 50 } }) } });
    assert(ctx.out.empty() && ctx.claimed.empty());
    assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots }));
    ctx = Poll(ultra, { { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 50 } }) } });
    assert(ctx.out.empty() && ctx.claimed.empty());
    assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots }));
    for (BYTE bad : { (BYTE)0x00, (BYTE)0x80, (BYTE)101, (BYTE)0xFF }) {
        ctx = Poll(ultra, { { { 0xB5, 0x03 }, UltraSlots(2, 1) },
                            { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, bad } }) } });
        assert(ctx.out.empty() && ctx.claimed.empty());
        assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots, kUltraAskBase }));
    }
    ctx = Poll(ultra, { { { 0xB5, 0x03 }, UltraSlots(0, 1) } });
    assert(ctx.out.empty() && ctx.claimed.empty());

    // Link-KM (D029) with a Keychron M6 (slot pid D064) answers the same B5 03 / B3 06 queries.
    auto linkKm = UltraCollection();
    linkKm.pid = 0xD029;
    ctx = Poll(linkKm, { { { 0xB5, 0x03 }, UltraReply(0xB5, 0x03, { { 2, 0x34 }, { 3, 0x34 }, { 4, 0x64 }, { 5, 0xD0 }, { 6, 1 } }) },
                         { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 66 } }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots, kUltraAskBase }));
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"keychron:D029" && ctx.out[0].name == L"Keychron M6");
    assert(ctx.out[0].percent == 66 && !ctx.out[0].charging && ctx.out[0].kind == DeviceKind::Mouse);

    // Other collections on D028 (e.g. FF0B:0104) are not opened by the Ultra-Link path.
    opens = 0;
    ctx = Poll(UltraCollection(0xFF0B), { { { 0xB5, 0x03 }, UltraSlots(0, 1) } });
    assert(ctx.out.empty() && writes.empty() && opens == 0);

    // Q6 Ultra 8K (3434:1260) behind the Ultra-Link 8K: B2 lists the slots (vid LE16, pid LE16, status
    // at bytes 2/7/12), A7 31 is answered with [battery %][charging]. The receiver is not claimed, and
    // the QMK keyboard path, which then sees the same slot, never sends A4 to an Ultra keyboard.
    const auto ultraRaw = RawCollection(0xD028);
    const auto slotsQ6 = RawReply({ 0xB2, 0x03, 0x34, 0x34, 0x60, 0x12, 0x01 });
    ctx = PollAll({ ultraRaw }, { { { 0xB2, 0x00 }, slotsQ6 }, { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 47, 1 }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kRawAskSlots, kRawAskUltra, kRawAskSlots }));
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"keychron-kb:1260" && ctx.out[0].name == L"Keychron Q6 Ultra 8K");
    assert(ctx.out[0].percent == 47 && ctx.out[0].charging && ctx.out[0].graceSec == 1800);
    assert(ctx.out[0].kind == DeviceKind::Keyboard && ctx.out[0].via == L"2.4 GHz receiver");
    assert(ctx.claimed.empty());

    // Stock firmware echoes A7 31 with an empty payload; a level or charging flag out of range, or a
    // reply to another sub-command, is not a battery reply either. Nothing listed, no A4.
    for (auto reply : { RawReply({ 0xA7, 0x31, 0, 0 }), RawReply({ 0xA7, 0x31, 101, 0 }), RawReply({ 0xA7, 0x31, 50, 2 }),
                        RawReply({ 0xA7, 0x0D, 47, 1 }) }) {
        ctx = PollAll({ ultraRaw }, { { { 0xB2, 0x00 }, slotsQ6 }, { { 0xA7, 0x31 }, reply } });
        assert((writes == std::vector<std::vector<BYTE>>{ kRawAskSlots, kRawAskUltra, kRawAskSlots }));
        assert(ctx.out.empty() && ctx.claimed.empty());
    }

    // Keyboard asleep (slot status 0) or no slot in use: the keyboard is not queried at all.
    for (auto slots : { RawReply({ 0xB2, 0x03, 0x34, 0x34, 0x60, 0x12, 0x00 }), RawReply({ 0xB2, 0x03 }) }) {
        ctx = PollAll({ ultraRaw }, { { { 0xB2, 0x00 }, slots }, { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 47, 1 }) } });
        assert((writes == std::vector<std::vector<BYTE>>{ kRawAskSlots, kRawAskSlots }));
        assert(ctx.out.empty() && ctx.claimed.empty());
    }
    // A product that is not a known Ultra keyboard is never sent A7 31 (the QMK path asks it A4 as before).
    ctx = PollAll({ ultraRaw }, { { { 0xB2, 0x00 }, RawReply({ 0xB2, 0x03, 0x34, 0x34, 0x99, 0x09, 0x01 }) },
                                  { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 47, 1 }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kRawAskSlots, kRawAskSlots, kRawAskQmk, kRawAskQmk }));
    assert(ctx.out.empty() && ctx.claimed.empty());

    // On FFC1 a keyboard in a slot is not taken for a mouse: B3 06, which it never answers, is not sent.
    const auto ffc1Q6 = UltraReply(0xB5, 0x03, { { 2, 0x34 }, { 3, 0x34 }, { 4, 0x60 }, { 5, 0x12 }, { 6, 1 } });
    ctx = PollAll({ ultra }, { { { 0xB5, 0x03 }, ffc1Q6 }, { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 50 } }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots }));
    assert(ctx.out.empty() && ctx.claimed.empty());

    // A mouse in the first slot (read on FFC1, which claims the receiver) and the keyboard in the second:
    // both are listed.
    ctx = PollAll({ ultra, ultraRaw },
                  { { { 0xB5, 0x03 }, UltraReply(0xB5, 0x03, { { 2, 0x34 }, { 3, 0x34 }, { 4, 0x49 }, { 5, 0xD0 }, { 6, 1 },
                                                               { 7, 0x34 }, { 8, 0x34 }, { 9, 0x60 }, { 10, 0x12 }, { 11, 1 } }) },
                    { { 0xB3, 0x06 }, UltraReply(0xB3, 0x06, { { 19, 0x37 } }) },
                    { { 0xB2, 0x00 }, RawReply({ 0xB2, 0x03, 0x34, 0x34, 0x49, 0xD0, 0x01, 0x34, 0x34, 0x60, 0x12, 0x01 }) },
                    { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 100, 0 }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kUltraAskSlots, kUltraAskBase, kRawAskSlots, kRawAskUltra }));
    assert(ctx.out.size() == 2 && ctx.out[0].key == L"keychron:D028" && ctx.out[0].percent == 55);
    assert(ctx.out[1].key == L"keychron-kb:1260" && ctx.out[1].percent == 100 && !ctx.out[1].charging);

    // The same keyboard on its USB cable: asked directly (no B2, no A4), same key, claimed.
    const auto q6Cable = RawCollection(0x1260);
    ctx = PollAll({ q6Cable }, { { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 51, 1 }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kRawAskUltra }));
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"keychron-kb:1260" && ctx.out[0].via == L"USB cable");
    assert(ctx.out[0].percent == 51 && ctx.out[0].charging && ctx.IsClaimed(0x3434, 0x1260));
    ctx = PollAll({ q6Cable }, { { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 0, 0 }) } });  // stock firmware
    assert((writes == std::vector<std::vector<BYTE>>{ kRawAskUltra }));
    assert(ctx.out.empty() && ctx.claimed.empty());

    // A QMK keyboard on its cable is not asked A7 31; it still goes through B2 / A4.
    ctx = PollAll({ RawCollection(0x0A50) }, { { { 0xA7, 0x31 }, RawReply({ 0xA7, 0x31, 51, 1 }) },
                                               { { 0xA4, 0x00 }, RawReply({ 0xA4, 52, 0x9F, 0x0E, 1 }) } });
    assert((writes == std::vector<std::vector<BYTE>>{ kRawAskSlots, kRawAskQmk }));
    assert(ctx.out.size() == 1 && ctx.out[0].key == L"keychron-kb:0A50" && ctx.out[0].name == L"Keychron K5 Max");
    assert(ctx.out[0].percent == 52 && ctx.out[0].millivolts == 3743 && ctx.out[0].charging);
    std::puts("Keychron driver tests passed (4K Link, 8K Nordic, 1K receiver, Ultra-Link 8K, Ultra 8K keyboard, no battery, filtering).");
}
