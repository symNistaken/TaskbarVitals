// VGN wireless mice and keyboards: read-only battery queries.
//
// Everything here comes from VGN's own HUB client (VGN VHUB 4.0.11). The Electron renderer ships
// its device code as plain JavaScript; the same bundle is served by their web client at
// drive.vgnlab.com.cn, so the request/reply layouts below are the vendor's, not a guess. Only the
// "Get…" commands are used: no mode changes, no flash writes, no lighting.
//
// The traps that cost the most time while mapping this (all verified against the JS):
//  * The client talks WebHID, whose e.data does not contain the report id; hidapi and HidDev keep
//    it. A one-byte shift here silently turns every level into 0, which is why each decoder below
//    strips the id explicitly instead of assuming a layout.
//  * The same vendor id exposes five to seven HID collections and only the vendor page answers.
//    Matching on vid/pid alone opens a keyboard or consumer-control collection that either times
//    out or fails the read outright.
//  * Two families answer "refused" with 0xFF in the length/sub-command position. That is what a
//    sleeping 2.4 GHz keyboard returns, so it must not be reported as 0%.
#include "vgn_protocol.h"

#include <algorithm>
#include <cstring>

namespace Vgn {

static const wchar_t* const kFamilyKeys[] = {
    L"da", L"king", L"kc2", L"ldms", L"yongjiaxin", L"tjx", L"weisheng", L"beiying", L"rongyuan",
};

const wchar_t* FamilyKey(Family f) {
    size_t i = (size_t)f;
    return i < sizeof(kFamilyKeys) / sizeof(kFamilyKeys[0]) ? kFamilyKeys[i] : L"vgn";
}

// ---------------------------------------------------------------------------------------------
// Device table. Product ids and their collection/usage/report-id triples are exported verbatim
// from the client's deviceList; the family column is the client's customDeviceType, i.e. which of
// its protocol classes handles the device.
// ---------------------------------------------------------------------------------------------
static const DeviceEntry kDevices[] = {
#include "vgn_table.inc"
};

const DeviceEntry* Find(uint16_t vid, uint16_t pid) {
    for (const DeviceEntry& d : kDevices)
        if (d.vid == vid && d.pid == pid) return &d;
    return nullptr;
}

namespace {

// A 16-byte Da-family frame ends with 85 - sum(bytes 0..14), minus the report id.
uint8_t DaChecksum(const uint8_t* frame, uint8_t reportId) {
    unsigned sum = 0;
    for (int i = 0; i < 15; i++) sum += frame[i];
    return (uint8_t)(((85 - (sum & 0xFF)) - reportId) & 0xFF);
}

// The rongyuan feature frame stores 255 - sum(bytes 0..6) at [7].
uint8_t RongyuanChecksum(const uint8_t* frame) {
    unsigned sum = 0;
    for (int i = 0; i < 7; i++) sum += frame[i];
    return (uint8_t)((255 - (sum & 0xFF)) & 0xFF);
}

// Reproduces HidDev's convention: a numbered report keeps its id at [0] (Read strips only a zero
// leading byte), so replies from report ids in `ids` need one byte skipped.
bool StartsWithReportId(const uint8_t* reply, size_t size, const uint8_t* ids, size_t count) {
    if (size < 2) return false;
    for (size_t i = 0; i < count; i++)
        if (ids[i] != 0 && reply[0] == ids[i]) return true;
    return false;
}

size_t SkipReportId(const uint8_t* reply, size_t size, uint8_t reportId) {
    return StartsWithReportId(reply, size, &reportId, 1) ? 1 : 0;
}

bool Plausible(int level) { return level >= 0 && level <= 100; }

}  // namespace

// ---------------------------------------------------------------------------------------------
// Requests. Each returns the number of bytes written to `out`; the caller prepends the report id
// and HidDev zero-pads to the collection's output/feature report length.
// ---------------------------------------------------------------------------------------------

size_t BuildDaRequest(uint8_t* out, size_t cap, uint8_t cmd, uint8_t reportId) {
    if (!out || cap < 16) return 0;
    memset(out, 0, 16);
    out[0] = cmd;  // 3 = DeviceOnLine, 4 = BatteryLevel
    out[15] = DaChecksum(out, reportId);
    return 16;
}

size_t BuildKingRequest(uint8_t* out, size_t cap, uint8_t cmd) {
    // [11][00][len=1][01][00][01][00][command] -- the client always sends a one-byte body.
    if (!out || cap < 8) return 0;
    memset(out, 0, 8);
    out[0] = 0x11;
    out[2] = 0x01;
    out[3] = 0x01;
    out[5] = 0x01;
    out[7] = cmd;  // 8 = GetUserMouseConfig (also carries the level)
    return 8;
}

size_t BuildKc2Request(uint8_t* out, size_t cap, uint8_t cmd) {
    if (!out || cap < 32) return 0;
    memset(out, 0, 32);
    out[0] = cmd;  // 6 = GetDeviceInfo
    return 32;
}

size_t BuildLdMsRequest(uint8_t* out, size_t cap, uint8_t cmd, uint8_t sub) {
    if (!out || cap < 32) return 0;
    memset(out, 0, 32);
    out[0] = cmd;  // 2 = GetDeviceInfo
    out[1] = sub;  // 37 = battery
    return 32;
}

size_t BuildYongjiaxinRequest(uint8_t* out, size_t cap, uint8_t cmd) {
    if (!out || cap < 64) return 0;
    memset(out, 0, 64);
    out[0] = 0x55;
    out[1] = cmd;  // 0x30 = GetBattery
    return 64;
}

size_t BuildTjxRequest(uint8_t* out, size_t cap, uint8_t cmd) {
    if (!out || cap < 63) return 0;
    memset(out, 0, 63);
    out[0] = cmd;   // 0x30 = GetBattery
    out[1] = 0x02;  // protocol version marker
    out[3] = 2;     // payload length; read requests leave the checksum at byte 2 as zero
    return 63;
}

size_t BuildWeishengRequest(uint8_t* out, size_t cap, uint8_t cmd) {
    // The client sends exactly seven bytes here, not the 520-byte length of the feature-report
    // families: [0][0][command][6][0][0][0].
    if (!out || cap < 7) return 0;
    out[0] = 0;
    out[1] = 0;
    out[2] = cmd;  // 26 = GetBatterLevel
    out[3] = 6;
    out[4] = 0;
    out[5] = 0;
    out[6] = 0;
    return 7;
}

size_t BuildBeiyingFeatureRequest(uint8_t* out, size_t cap, uint8_t cmd) {
    // 520-byte feature report: [cmd][0][0][1][0][2][0] then zero padding.
    if (!out || cap < 520) return 0;
    memset(out, 0, 520);
    out[0] = cmd;  // 135 = GetBattery on the wired N75V2 / V87Pro
    out[3] = 0x01;
    out[5] = 0x02;
    return 520;
}

size_t BuildBeiyingOutputRequest(uint8_t* out, size_t cap, uint8_t cmd, uint8_t sub, uint8_t reportId) {
    // 19-byte frame for the 2.4 GHz dongle channel, closed by (reportId + sum) & 0xFF.
    if (!out || cap < 19) return 0;
    memset(out, 0, 19);
    out[0] = cmd;  // 74 = battery
    out[1] = sub;
    unsigned sum = reportId;
    for (int i = 0; i < 18; i++) sum += out[i];
    out[18] = (uint8_t)(sum & 0xFF);
    return 19;
}

size_t BuildRongyuanRequest(uint8_t* out, size_t cap, uint8_t cmd) {
    if (!out || cap < 64) return 0;
    memset(out, 0, 64);
    out[0] = cmd;  // 130 = GetBatterLevel
    out[7] = RongyuanChecksum(out);
    return 64;
}

// ---------------------------------------------------------------------------------------------
// Replies
// ---------------------------------------------------------------------------------------------

Reading DecodeDa(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    if (!reply || size < off + 9) return r;
    const uint8_t* d = reply + off;
    if (d[0] != 4) return r;  // not a BatteryLevel reply
    int level = d[5];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    r.charging = d[6] == 1;
    r.millivolts = (d[7] << 8) | d[8];
    return r;
}

Reading DecodeKing(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    // The client slices the reply at byte 7 and reads the config block from there; the level is
    // nine bytes into that block.
    if (!reply || size < off + 7 + 10) return r;
    const uint8_t* d = reply + off + 7;
    if (d[0] != 8) return r;  // GetUserMouseConfig
    int level = d[9];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    r.wired = (d[8] & 0x02) == 0;
    r.charging = r.wired;
    return r;
}

Reading DecodeKc2(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    if (!reply || size < off + 20) return r;
    const uint8_t* d = reply + off;
    if (d[0] != 6) return r;  // GetDeviceInfo
    int level = 127 & d[19];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    return r;
}

Reading DecodeLdMs(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    if (!reply || size < off + 3) return r;
    const uint8_t* d = reply + off;
    // Either the solicited GetDeviceInfo reply (code 2, sub 37) or the unsolicited 0x50 status.
    if (d[0] == 2 && d[1] == 37) {
        int level = d[2];
        if (!Plausible(level)) return r;
        r.answered = true;
        r.percent = level;
        return r;
    }
    if (d[0] == 0x50 && size >= off + 8) {
        int level = d[7];
        if (!Plausible(level)) return r;
        r.answered = true;
        r.percent = level;
        r.wired = d[1] == 0 && d[2] == 0;
        r.charging = r.wired;
        return r;
    }
    return r;
}

Reading DecodeYongjiaxin(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    if (!reply || size < off + 10) return r;
    const uint8_t* d = reply + off;
    if (d[0] != 0x55 || d[1] != 0x30) return r;
    // The client jumps to byte 8 of the frame: [0] is the level, [1] is set on a cable link.
    int level = d[8];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    r.wired = d[9] == 1;
    r.charging = r.wired;
    return r;
}

Reading DecodeTjx(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    if (!reply || size < off + 9) return r;
    const uint8_t* d = reply + off;
    if (d[0] != 0x30 || d[1] != 0x02) return r;
    if (d[3] < 2) return r;  // a zero-length payload is not a battery answer
    int level = d[7];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    r.charging = d[8] == 1;
    return r;
}

Reading DecodeWeisheng(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    size_t off = SkipReportId(reply, size, reportId);
    if (!reply || size < off + 9) return r;
    const uint8_t* d = reply + off;
    if (d[2] != 26) return r;    // GetBatterLevel echoes at +2 for this family
    if (d[3] == 0xFF) return r;  // refused: the board is asleep (or its config block is invalid)
    int level = d[7];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    return r;
}

Reading DecodeBeiyingFeature(const uint8_t* reply, size_t size, uint8_t /*reportId*/) {
    Reading r;
    static const uint8_t ids[] = { 6, 9 };
    size_t off = StartsWithReportId(reply, size, ids, sizeof(ids)) ? 1 : 0;
    if (!reply || size < off + 9) return r;
    // A feature report has no command echo at [0]: the level is simply at +8.
    int level = reply[off + 8];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    return r;
}

Reading DecodeBeiyingOutput(const uint8_t* reply, size_t size, uint8_t /*reportId*/) {
    Reading r;
    static const uint8_t ids[] = { 19 };
    size_t off = StartsWithReportId(reply, size, ids, sizeof(ids)) ? 1 : 0;
    if (!reply || size < off + 5) return r;
    const uint8_t* d = reply + off;
    if (d[0] != 0x4A) return r;  // battery reply marker
    int level = d[4];
    if (!Plausible(level)) return r;
    r.answered = true;
    r.percent = level;
    return r;
}

Reading DecodeRongyuan(const uint8_t* reply, size_t size, uint8_t reportId) {
    Reading r;
    // This family talks over feature report id 0, whose leading id byte HidDev drops; a reply that
    // still carries an id therefore has to keep it rather than skip another byte.
    size_t off = 0;
    (void)reportId;
    if (!reply || size <= off) return r;
    const uint8_t* d = reply + off;
    // GetBatterLevel(130) is normally answered with the command echoed in front of the level. Some
    // SKUs send the bare level instead, so that shape is accepted too -- but only while the value
    // is a percentage, so a stray status frame cannot be mistaken for 0x51 = 81%.
    if (size >= off + 2 && d[0] == 130 && Plausible(d[1])) {
        r.answered = true;
        r.percent = d[1];
        return r;
    }
    if (Plausible(d[0]) && d[0] != 130) {
        r.answered = true;
        r.percent = d[0];
        return r;
    }
    return r;
}

}  // namespace Vgn
