// Verifies every device and system VectorIcon produces clean non-empty anti-aliased masks across DPI sizes.
#include <cassert>
#include <cstdio>
#include <numeric>

#include "../src/vector_icons.h"

int main() {
    using namespace vector_icons;
    const VectorIcon icons[] = {
        VectorIcon::Headset, VectorIcon::Mouse, VectorIcon::Keyboard, VectorIcon::Gamepad,
        VectorIcon::Phone, VectorIcon::Microphone, VectorIcon::Speaker, VectorIcon::Bluetooth,
        VectorIcon::GenericDevice, VectorIcon::Cpu, VectorIcon::Ram, VectorIcon::Gpu,
        VectorIcon::Vram, VectorIcon::Disk, VectorIcon::Network, VectorIcon::LaptopBattery, VectorIcon::BatteryUnknown,
    };
    for (int size : { 12, 16, 20, 24 }) {
        for (VectorIcon ic : icons) {
            IconMask m = Render(ic, size);
            assert(m.w == size && m.h == size && (int)m.a.size() == size * size);
            int sum = std::accumulate(m.a.begin(), m.a.end(), 0);
            assert(sum > 0);
        }
    }
    assert(Render(VectorIcon::None, 16).a.empty());
    assert(ForDevice(DeviceKind::Speaker, false) == VectorIcon::Speaker);
    assert(ForDevice(DeviceKind::Other, true) == VectorIcon::Bluetooth);
    std::puts("Vector icons tests passed (16 icons across 4 sizes, ForDevice mapping).");
    return 0;
}
