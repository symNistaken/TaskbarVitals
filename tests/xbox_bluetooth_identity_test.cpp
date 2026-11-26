// Test the exact Bluetooth deduplication logic used by PollBluetooth with synthetic
// Windows device-container IDs. No Xbox controllers or Win32 API mocking required.
#include "../src/drivers/bluetooth_identity.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {
DeviceStatus Bt(const std::wstring& containerId, const std::wstring& name, int level) {
    DeviceStatus status;
    status.key = L"bt:" + containerId;
    status.name = name;
    status.via = L"Bluetooth";
    status.kind = DeviceKind::Gamepad;
    status.percent = level;
    return status;
}

bool AddIfDistinct(std::vector<DeviceStatus>& out, const DeviceStatus& candidate) {
    if (IsDuplicateBluetoothDevice(candidate, out)) return false;
    out.push_back(candidate);
    return true;
}
}  // namespace

int main() {
    // Reported scenario: two Xbox Wireless Controllers on one Bluetooth motherboard.
    // Both share the same name, but each has its own Windows container ID.
    const std::wstring firstId = L"{4ECA5F3F-7FD8-5F0A-96FD-CFE00AC01DAA}";
    const std::wstring secondId = L"{44444444-1111-2222-3333-555555555555}";
    auto first = Bt(firstId, L"Xbox Wireless Controller", 78);
    auto second = Bt(secondId, L"Xbox Wireless Controller", 56);

    std::vector<DeviceStatus> out;
    assert(AddIfDistinct(out, first));
    assert(AddIfDistinct(out, second));
    assert(out.size() == 2 && out[0].name == out[1].name);
    assert(out[0].key != out[1].key && out[0].percent == 78 && out[1].percent == 56);

    // The same physical controller appearing again must not produce a second entry,
    // even if Windows gives it a different display name or battery level.
    auto duplicateId = Bt(firstId, L"Xbox Wireless Controller (renamed)", 80);
    assert(!AddIfDistinct(out, duplicateId));
    assert(out.size() == 2);

    // The second device still appears regardless of enumeration order.
    std::vector<DeviceStatus> reversed;
    assert(AddIfDistinct(reversed, second));
    assert(AddIfDistinct(reversed, first));
    assert(reversed.size() == 2 && reversed[0].key == second.key && reversed[1].key == first.key);

    // An unrelated Bluetooth device (JBL) coexists with both controllers.
    auto jbl = Bt(L"{BBBBBBBB-1111-2222-3333-444444444444}", L"JBL Boombox", 65);
    jbl.kind = DeviceKind::Headset;
    assert(AddIfDistinct(out, jbl));
    assert(out.size() == 3);

    // A vendor driver that reports over Bluetooth itself (Galaxy Buds) still hides
    // Windows' entry with the same name.
    std::vector<DeviceStatus> buds;
    DeviceStatus budsDriver = Bt(L"x", L"Galaxy Buds3 Pro", 70);
    budsDriver.key = L"galaxybuds:x";
    buds.push_back(budsDriver);
    assert(!AddIfDistinct(buds, Bt(L"{CCCCCCCC-1111-2222-3333-444444444444}", L"Galaxy Buds3 Pro", 70)));

    // Preserve the existing guard for vendor drivers that already reported
    // a device by name, e.g. when no matching HID container can be obtained.
    std::vector<DeviceStatus> vendor;
    DeviceStatus existing;
    existing.key = L"vendor:1234";
    existing.name = L"Other wireless mouse";
    existing.via = L"2.4 GHz receiver";
    vendor.push_back(existing);
    assert(!AddIfDistinct(vendor, Bt(L"{AAAAAAAA}", L"Other wireless mouse", 20)));
    assert(vendor.size() == 1);

    // Different device names must not be affected by the vendor fallback.
    assert(AddIfDistinct(vendor, Bt(L"{CCCCCCCC}", L"Other wireless keyboard", 95)));
    assert(vendor.size() == 2);

    // Physical identity is stronger than a display name, even across sources.
    std::vector<DeviceStatus> colliding;
    existing.key = L"bt:{CCCCCCCC}";
    existing.name = L"Some other name";
    existing.via = L"HID";
    colliding.push_back(existing);
    assert(!AddIfDistinct(colliding, Bt(L"{CCCCCCCC}", L"Xbox Wireless Controller", 95)));
    assert(colliding.size() == 1);

    // A Logitech mouse over Bluetooth: HID++ read it last poll, missed it this poll. Windows' level keeps
    // the vendor card (same key, name and link) instead of adding a second "Bluetooth" card.
    DeviceStatus logi;
    logi.key = L"logi:unit:12345678";
    logi.name = L"MX Master 3S";
    logi.via = L"USB / Bluetooth";
    logi.kind = DeviceKind::Mouse;
    logi.percent = 80;
    logi.millivolts = 3900;
    logi.charging = true;
    logi.full = true;
    logi.asleep = true;
    DeviceStatus standIn = StandInForVendor(logi, 75);
    assert(standIn.key == logi.key && standIn.name == logi.name && standIn.via == logi.via);
    assert(standIn.kind == DeviceKind::Mouse && standIn.percent == 75 && standIn.millivolts == 0);
    assert(standIn.connected && !standIn.asleep && standIn.charging && !standIn.full);
    assert(StandInForVendor(logi, 100).full);

    std::puts("Xbox Bluetooth identity regression tests passed (two controllers, same ID, vendor fallback, JBL, vendor stand-in).");
}
