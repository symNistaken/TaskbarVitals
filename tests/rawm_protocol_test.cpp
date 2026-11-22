#include "../src/drivers/rawm_protocol.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Splits a byte stream into input reports the way the receiver sends it (report ID stripped):
// [0xC0 | channel] [0x80 | size] payload, `chunk` payload bytes per report, zero padded to 64.
std::vector<std::vector<std::uint8_t>> Reports(const std::vector<std::uint8_t>& stream, size_t chunk, int channel) {
    std::vector<std::vector<std::uint8_t>> out;
    for (size_t o = 0; o < stream.size(); o += chunk) {
        size_t n = std::min(chunk, stream.size() - o);
        std::vector<std::uint8_t> r;
        if (channel >= 0) r.push_back((std::uint8_t)(0xC0 | channel));
        r.push_back((std::uint8_t)(0x80 | n));
        r.insert(r.end(), stream.begin() + o, stream.begin() + o + n);
        r.resize(64, 0x5A);  // stale bytes after the payload must be ignored
        out.push_back(r);
    }
    return out;
}

std::vector<std::uint8_t> Event(std::uint8_t cmd, const std::vector<std::uint8_t>& body) {
    size_t len = body.size() + 2;
    std::vector<std::uint8_t> e{ 0xFF, 0xFF, 0xFF, 0xFF, (std::uint8_t)(cmd | ((len >> 4) & 0xF0)), (std::uint8_t)len };
    e.insert(e.end(), body.begin(), body.end());
    return e;
}

std::vector<std::uint8_t> Text(const std::string& s) {
    std::vector<std::uint8_t> v(s.begin(), s.end());
    v.push_back(0);
    return v;
}

}  // namespace

int main() {
    // Output framing captured from RAWM HUB's send_event_query / send_event_ping and accepted by the
    // RAWM HS Receiver (1915:2346): 00 8D 01 0D 03 00 00 <time LE>, and C0 for the mouse channel.
    auto q = Rawm::Packet(Rawm::QueryEvent(0x6AFC83D1ull));
    const std::vector<std::uint8_t> wantQ = { 0x00, 0x8D, 0x01, 0x0D, 0x03, 0x00, 0x00, 0xD1, 0x83, 0xFC, 0x6A, 0, 0, 0, 0 };
    assert(q == wantQ);
    auto p = Rawm::Packet(Rawm::PingEvent(), 0);
    const std::vector<std::uint8_t> wantP = { 0x00, 0xC0, 0x84, 0x0E, 0x04, 0x00, 0x00 };
    assert(p == wantP);

    // Receiver info (captured; ESB address zeroed), 63 bytes per report, then the Leviathan V4's
    // info (captured layout, header 22 26 = 550 bytes, shortened here) in 30-byte ESB chunks.
    Rawm::Link link;
    auto rx = Event(Rawm::kCmdQueryResult,
                    Text("{\"r\":\"2.0.2\",\"rc\":4,\"dn\":\"RAWM HS Receiver\",\"pi\":9030,\"vi\":6421,\"msg\":\"\",\"cn\":0,"
                         "\"esb_addr\":\"00000000000000000000000000000000\",\"esb_ch\":255,\"light\":1,\"slow\":0}"));
    for (auto& r : Reports(rx, 63, -1)) link.Feed(r.data(), r.size());
    assert(link.direct.status().haveInfo && link.direct.status().percent == -1);
    assert(Rawm::JsonString(link.direct.status().json, "dn") == "RAWM HS Receiver");
    assert(!link.channel0.status().haveInfo);

    // Flow-control packet seen before the mouse data: C0 52 FB 0B 00 00 ... (ignored).
    std::vector<std::uint8_t> flow = { 0xC0, 0x52, 0xFB, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0C };
    link.Feed(flow.data(), flow.size());

    std::string mouse =
        "{\"r\":\"1.2.0\",\"rc\":6,\"dn\":\"LEVIATHAN V4\",\"pi\":9034,\"vi\":6421,\"battery\":100,\"chr\":0,\"cpi\":1600,"
        "\"polling\":1000,\"light\":48,\"cpi_l\":[400,800,1600,3200,0,0,0,0],\"esb_addr\":\"00000000000000000000000000000000\","
        "\"msg\":\"\",\"cn\":8,\"pm\":2,\"esb_ch\":0,\"crc\":1,\"sst\":\"PAW3950\",\"rr\":\"1.0.0\",\"rrc\":1}";
    while (mouse.size() < 547) mouse.insert(mouse.size() - 1, " ");  // keep the captured 2-byte length form
    auto info = Event(Rawm::kCmdQueryResult, Text(mouse));
    assert(info[4] == 0x22 && info[5] == 0x26);
    auto ping = Event(Rawm::kCmdPing, { 0x8E, 0x00, 0x00 });                       // captured ping reply
    auto config = Event(Rawm::kCmdNotify, { 0x14, 0x03, 0x08, 0x16, 0x01, 0x0A, 0x00, 0x01, 0x01 });  // captured
    std::vector<std::uint8_t> stream = info;
    stream.insert(stream.end(), ping.begin(), ping.end());
    stream.insert(stream.end(), config.begin(), config.end());
    auto reports = Reports(stream, 30, 0);
    for (size_t i = 0; i + 1 < reports.size(); i++) {
        link.Feed(reports[i].data(), reports[i].size());
        if (i < 10) assert(!link.channel0.status().haveInfo);  // not complete yet
    }
    link.Feed(reports.back().data(), reports.back().size());
    const auto& st = link.channel0.status();
    assert(st.haveInfo && st.percent == 100 && !st.charging);
    long pid = 0;
    assert(Rawm::JsonInt(st.json, "pi", pid) && pid == 0x234A);
    assert(Rawm::JsonString(st.json, "dn") == "LEVIATHAN V4");
    assert(link.direct.status().percent == -1);  // channel data never leaks into the receiver

    // NOTIFY_TYPE_MOUSE_BATTERY: [0x17, percent, charging].
    auto notify = Event(Rawm::kCmdNotify, { 0x17, 63, 1 });
    for (auto& r : Reports(notify, 30, 0)) link.Feed(r.data(), r.size());
    assert(link.channel0.status().percent == 63 && link.channel0.status().charging);
    auto bad = Event(Rawm::kCmdNotify, { 0x17, 101, 0 });
    for (auto& r : Reports(bad, 30, 0)) link.Feed(r.data(), r.size());
    assert(link.channel0.status().percent == 63);

    // Other channels are not the mouse.
    std::vector<std::uint8_t> other = { 0xC1, 0x86, 0xFF, 0xFF, 0xFF, 0xFF, 0x0B, 0x05 };
    link.Feed(other.data(), other.size());
    assert(link.channel0.status().percent == 63);

    std::puts("rawm protocol test passed");
    return 0;
}
