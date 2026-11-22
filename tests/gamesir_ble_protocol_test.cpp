#include "../src/drivers/gamesir_ble_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    // 19-byte base-info packet: MAC, versions, build date, battery last.
    std::vector<std::uint8_t> p(19, 0x11);
    GameSirBle::Reading r;

    p[18] = 73;
    assert(GameSirBle::Parse(p.data(), p.size(), r) && r.percent == 73 && !r.charging);

    p[18] = 0xFF;  // charging: no level
    assert(GameSirBle::Parse(p.data(), p.size(), r) && r.percent == -1 && r.charging);

    p[18] = 101;   // not a battery value
    assert(!GameSirBle::Parse(p.data(), p.size(), r));

    // Some firmwares prefix the packet with C9.
    std::vector<std::uint8_t> q(20, 0x22);
    q[0] = 0xC9;
    q[19] = 40;
    assert(GameSirBle::Parse(q.data(), q.size(), r) && r.percent == 40);

    // Other notifications (wrong length, or 20 bytes without the prefix) are ignored.
    q[0] = 0x00;
    assert(!GameSirBle::Parse(q.data(), q.size(), r));
    assert(!GameSirBle::Parse(p.data(), 2, r));

    std::puts("gamesir_ble_protocol_test: ok");
    return 0;
}
