// VGN mice and keyboards (VID 0x3554, 0x391D, 0x320F, 0x258A, 0x3151, 0xA8A4, 0x25AA, 0x1CA6, 0x372E,
// 0x9981, 0x3837, 0x373B) -- battery levels, read-only.
//
// Layouts are taken from VGN's own HUB client (VGN VHUB 4.0.11): its Electron renderer carries the
// device classes as plain JavaScript, and the vendor serves the same bundle as their web client at
// drive.vgnlab.com.cn. The product ids, their HID collections and the report ids in vgn_table.inc
// are exported verbatim from that client's deviceList, and every request below is a "Get…" command
// from the matching class. Nothing here changes a mode, a profile, lighting or flash.
//
// Notes that saved the most time, and the mistakes they prevent:
//  * VGN keeps its settings in per-family blobs, so there is no single battery command: the level
//    lives inside a "device info" or "configuration" reply, at a different offset per family.
//  * The client uses WebHID, whose reports do not include the report id; hidapi and HidDev keep
//    it. Every decoder here strips it explicitly -- a one-byte shift turns levels into 0.
//  * One vendor id exposes five to seven HID collections and only the vendor page answers. The
//    table therefore pins page/usage/report id, not just vid/pid.
//  * A sleeping 2.4 GHz VGN keyboard answers with 0xFF where a length belongs. That is not 0%.
//  * These models report a level over the 2.4 GHz link only. The table keeps the cable ids so the
//    product is recognised and claimed, but a device on a cable reports nothing here.
#include "drivers.h"
#include "vgn_protocol.h"

#include <algorithm>
#include <cstring>

namespace {

using Vgn::Channel;
using Vgn::DeviceEntry;
using Vgn::Family;
using Vgn::Reading;

// Report ids a family may answer on (reads and writes use different ids on the kc2 mice).
struct Ids {
    uint8_t first;
    uint8_t second;
};

// The kc2 mice keep reportId 0 in the client's device list and pick 179/181 at runtime, so the id
// used for the wire is not always the one in the table.
uint8_t WireReportId(const DeviceEntry& e) { return e.family == Family::Kc2 ? 179 : e.reportId; }

Ids ReplyIds(const DeviceEntry& e) {
    switch (e.family) {
        case Family::Kc2: return { 179, 181 };
        case Family::Beiying: return e.channel == Channel::Feature ? Ids{ 6, 9 } : Ids{ 19, 0 };
        default: return { WireReportId(e), 0 };
    }
}

size_t BuildRequest(const DeviceEntry& e, uint8_t* out, size_t cap) {
    switch (e.family) {
        case Family::Da: return Vgn::BuildDaRequest(out, cap, 4, WireReportId(e));
        case Family::King: return Vgn::BuildKingRequest(out, cap, 8);
        case Family::Kc2: return Vgn::BuildKc2Request(out, cap, 6);
        case Family::LdMs: return Vgn::BuildLdMsRequest(out, cap, 2, 37);
        case Family::Yongjiaxin: return Vgn::BuildYongjiaxinRequest(out, cap, 0x30);
        case Family::Tjx: return Vgn::BuildTjxRequest(out, cap, 0x30);
        case Family::Weisheng: return Vgn::BuildWeishengRequest(out, cap, 26);
        case Family::Beiying:
            return e.channel == Channel::Feature
                       ? Vgn::BuildBeiyingFeatureRequest(out, cap, 135)
                       : Vgn::BuildBeiyingOutputRequest(out, cap, 0x4A, 0x01, WireReportId(e));
        case Family::Rongyuan: return Vgn::BuildRongyuanRequest(out, cap, 130);
    }
    return 0;
}

Reading Decode(const DeviceEntry& e, const uint8_t* reply, size_t size) {
    const Ids ids = ReplyIds(e);
    auto pick = [&](uint8_t reportId) {
        switch (e.family) {
            case Family::Da: return Vgn::DecodeDa(reply, size, reportId);
            case Family::King: return Vgn::DecodeKing(reply, size, reportId);
            case Family::Kc2: return Vgn::DecodeKc2(reply, size, reportId);
            case Family::LdMs: return Vgn::DecodeLdMs(reply, size, reportId);
            case Family::Yongjiaxin: return Vgn::DecodeYongjiaxin(reply, size, reportId);
            case Family::Tjx: return Vgn::DecodeTjx(reply, size, reportId);
            case Family::Weisheng: return Vgn::DecodeWeisheng(reply, size, reportId);
            case Family::Beiying:
                return e.channel == Channel::Feature ? Vgn::DecodeBeiyingFeature(reply, size, reportId)
                                                     : Vgn::DecodeBeiyingOutput(reply, size, reportId);
            case Family::Rongyuan: return Vgn::DecodeRongyuan(reply, size, reportId);
        }
        return Reading{};
    };
    Reading r = pick(ids.first);
    if (!r.answered && ids.second) r = pick(ids.second);
    return r;
}

std::vector<BYTE> WithReportId(uint8_t reportId, const uint8_t* body, size_t len) {
    std::vector<BYTE> w;
    w.reserve(len + 1);
    w.push_back(reportId);
    w.insert(w.end(), body, body + len);
    return w;
}

// Reads input reports until one decodes, or the deadline passes. Unrelated frames (mouse motion,
// key presses) are expected and simply skipped.
bool ReadReading(HidDev& d, const DeviceEntry& e, Reading& out, int timeoutMs) {
    ULONGLONG end = GetTickCount64() + (ULONGLONG)timeoutMs;
    for (;;) {
        LONGLONG left = (LONGLONG)end - (LONGLONG)GetTickCount64();
        if (left <= 0) return false;
        std::vector<BYTE> r;
        if (d.Read(r, (int)left) < 0) return false;
        Reading got = Decode(e, r.data(), r.size());
        if (got.answered) {
            out = got;
            return true;
        }
    }
}

// Output-report query: write, then wait for the matching input report.
bool QueryOutput(HidDev& d, const DeviceEntry& e, Reading& out, int timeoutMs) {
    uint8_t body[560] = {};
    size_t len = BuildRequest(e, body, sizeof(body));
    if (!len) return false;
    d.Flush();
    if (!d.Write(WithReportId(WireReportId(e), body, len))) return false;
    return ReadReading(d, e, out, timeoutMs);
}

// Feature-report query: SetFeature carries the request, GetFeature returns the answer.
bool QueryFeature(HidDev& d, const DeviceEntry& e, Reading& out, int timeoutMs) {
    uint8_t body[560] = {};
    size_t len = BuildRequest(e, body, sizeof(body));
    if (!len) return false;
    if (!d.SetFeature(WithReportId(WireReportId(e), body, len))) return false;
    // The keyboard families assemble the answer after the request arrives.
    Sleep(std::min(timeoutMs, 400) / 4);
    for (int attempt = 0; attempt < 3; attempt++) {
        std::vector<BYTE> r;
        if (!d.GetFeature(WireReportId(e), r)) return false;
        Reading got = Decode(e, r.data(), r.size());
        if (got.answered) {
            out = got;
            return true;
        }
        Sleep(60);
    }
    return false;
}

// The Neon75 family parks device replies in the receiver until they are fetched with 0xF7 (read
// the queue status, whose byte 1 is the receiver's own view of the level) and 0xFC (take the
// device payload). Both are read-only; without them a 2.4 GHz keyboard never answers.
bool RongyuanFetch(HidDev& d, Reading& out) {
    uint8_t frame[64] = {};
    frame[0] = 0xF7;
    frame[7] = (uint8_t)((255 - (unsigned)frame[0]) & 0xFF);
    if (!d.SetFeature(WithReportId(0, frame, 64))) return false;
    Sleep(150);
    std::vector<BYTE> r;
    if (!d.GetFeature(0, r) || r.size() < 2) return false;
    // r[0] is the report id 0 that hidapi keeps for numbered reads; GetFeature(0) returns the
    // report starting at its own id byte, so the level is at r[1] and r[5] says "busy" when 0.
    int level = r.size() > 1 ? r[1] : -1;
    if (level >= 0 && level <= 100) {
        out.answered = true;
        out.percent = level;
    }
    // Queue the device payload so the following GetFeature(130) can decode it.
    frame[0] = 0xFC;
    frame[7] = (uint8_t)((255 - (unsigned)frame[0]) & 0xFF);
    d.SetFeature(WithReportId(0, frame, 64));
    Sleep(30);
    return out.answered;
}

// Picks the collection that actually speaks the family's protocol: the exact vendor page from the
// client's device list, plus a report length big enough for the request.
std::vector<const HidColl*> Candidates(const PollContext& ctx, const DeviceEntry& e) {
    return ctx.Find(e.vid, e.pid, [&](const HidColl& c) {
        if (c.page() != e.usagePage || c.usage() != e.usage) return false;
        if (e.channel == Channel::Feature) return c.featLen() >= 9;
        return c.outLen() >= 8 && c.inLen() >= 8;
    });
}

std::wstring KindName(const DeviceEntry& e, const std::wstring& product) {
    switch (e.family) {
        case Family::Tjx:
        case Family::Weisheng:
        case Family::Beiying:
        case Family::Rongyuan:
            return product.empty() ? L"VGN wireless keyboard" : product;
        default:
            return product.empty() ? L"VGN wireless mouse" : product;
    }
}

DeviceKind KindOf(const DeviceEntry& e) {
    switch (e.family) {
        case Family::Tjx:
        case Family::Weisheng:
        case Family::Beiying:
        case Family::Rongyuan:
            return DeviceKind::Keyboard;
        default:
            return DeviceKind::Mouse;
    }
}

}  // namespace

void PollVgnClient(PollContext& ctx) {
    // One attempt per distinct product id; the table holds both the wired and the dongle ids.
    std::vector<std::pair<USHORT, USHORT>> products;
    for (const auto& c : ctx.colls) {
        if (Vgn::Find(c.vid, c.pid) == nullptr) continue;
        auto key = std::make_pair(c.vid, c.pid);
        if (std::find(products.begin(), products.end(), key) == products.end()) products.push_back(key);
    }

    for (auto [vid, pid] : products) {
        const DeviceEntry* e = Vgn::Find(vid, pid);
        if (!e || ctx.IsClaimed(vid, pid)) continue;

        for (const HidColl* c : Candidates(ctx, *e)) {
            HidDev d;
            const bool feature = e->channel == Channel::Feature;
            if (feature ? !d.OpenFeatureOnly(*c) : !d.Open(*c)) continue;

            Reading r;
            bool ok = feature ? QueryFeature(d, *e, r, 1000) : QueryOutput(d, *e, r, 1000);
            // The Da mice want an online probe before the level; a sleeping mouse ignores the
            // first request. One retry covers that without stretching the poll budget.
            if (!ok) {
                if (e->family == Family::Da) {
                    uint8_t probe[16];
                    if (Vgn::BuildDaRequest(probe, sizeof(probe), 3, WireReportId(*e)))
                        d.Write(WithReportId(WireReportId(*e), probe, 16));
                    ReadReading(d, *e, r, 300);
                } else if (e->family == Family::Rongyuan) {
                    RongyuanFetch(d, r);
                    ok = r.answered;
                }
                if (!ok && e->family != Family::Rongyuan)
                    ok = feature ? QueryFeature(d, *e, r, 900) : QueryOutput(d, *e, r, 900);
            }
            if (!ok) continue;

            DeviceStatus s;
            s.key = std::wstring(L"vgn:") + Hex4(vid) + L":" + Hex4(pid) + L":"
                    + Vgn::FamilyKey(e->family);
            s.name = KindName(*e, HidProduct(*c));
            s.kind = KindOf(*e);
            s.via = e->receiver ? L"2.4 GHz receiver" : (r.wired ? L"USB cable" : L"USB");
            s.percent = r.percent;
            s.charging = r.charging;
            s.millivolts = r.millivolts;
            // VGN devices only refresh their level every few seconds; keep the last value across
            // the 60 s poll when the device naps.
            s.graceSec = 1800;
            ctx.Add(s, vid, pid);
            break;
        }
    }
}
