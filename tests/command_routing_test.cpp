// Portable tests for menu command IDs, non-overlapping ranges, and pure command routing decoders.
#include "../src/command_routing.h"

#include <cassert>
#include <cstdio>

namespace {

void TestIdRangesDoNotOverlap() {
    using namespace command_routing;
    assert(ID_PLACE_BOTTOM < ID_UPDATE);
    assert(ID_LOCAL_API_PIPE < ID_MONITOR_ALL);
    assert(ID_MONITOR_FIRST + 32u <= ID_LANG_AUTO);
    assert(ID_LANG_FIRST + 64u <= ID_POLL_FIRST);
    assert(ID_POLL_FIRST + 8u <= ID_SYS_POLL_FIRST);
    assert(ID_SYS_POLL_FIRST + 4u <= ID_LIMIT_OFF);
    assert(ID_LIMIT_FIRST + 4u <= ID_EVENING_REMINDER_OFF);
    assert(ID_EVENING_REMINDER_FIRST + 4u <= ID_OSD_MODE_FIRST);
    assert(ID_OSD_MODE_FIRST + 4u <= ID_OSD_CORNER_FIRST);
    assert(ID_OSD_CORNER_FIRST + 4u <= ID_ALERT_SOUND_FIRST);
    assert(ID_ALERT_SOUND_FIRST + 2u <= ID_TEMP_ALERT_OFF);
    assert(ID_TEMP_ALERT_FIRST + 10u <= ID_TEMP_PRESET_FIRST);
    assert(ID_TEMP_PRESET_FIRST + 3u <= ID_POSITION_FIRST);
    assert(ID_POSITION_FIRST + 7u <= ID_SYS_MOVE_FIRST);
    assert(ID_SYS_MOVE_FIRST + 16u <= ID_POPUP_CHARGE_LIMIT);
    assert(ID_POPUP_EVENING_REMINDER < ID_UNIGNORE_FIRST);
    assert(ID_UNIGNORE_FIRST + 64u <= ID_DEVICE_FIRST);
    assert(DEV_IGNORE < DEV_STRIDE);
}

void TestDeviceCommands() {
    using namespace command_routing;
    DecodedDeviceCmd out;

    assert(!DecodeDeviceCommand(ID_DEVICE_FIRST, 0, out));
    assert(!DecodeDeviceCommand(ID_DEVICE_FIRST - 1u, 2, out));
    assert(!DecodeDeviceCommand(ID_DEVICE_FIRST + 2u * DEV_STRIDE, 2, out));
    assert(!DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_IGNORE + 1u, 2, out));
    assert(!DecodeDeviceCommand(ID_DEVICE_FIRST + 31u, 2, out));

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_TOGGLE, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_TOGGLE && out.alertSlot == -1);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_STRIDE + DEV_LEFT, 2, out));
    assert(out.deviceIndex == 1 && out.subCmd == DEV_LEFT && out.alertSlot == -1);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_STRIDE + DEV_RIGHT, 2, out));
    assert(out.deviceIndex == 1 && out.subCmd == DEV_RIGHT && out.alertSlot == -1);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_RENAME, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_RENAME);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_RESET_NAME, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_RESET_NAME);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_HISTORY, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_HISTORY);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_ALERT_DEFAULT, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_ALERT_DEFAULT);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_ALERT_OFF, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_ALERT_OFF);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_MORE_POPUP, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_MORE_POPUP);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_DASHBOARD, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_DASHBOARD);

    assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_IGNORE, 2, out));
    assert(out.deviceIndex == 0 && out.subCmd == DEV_IGNORE);

    for (int slot = 0; slot < alerts::kThresholdCount; ++slot) {
        assert(DecodeDeviceCommand(ID_DEVICE_FIRST + DEV_STRIDE + DEV_ALERT_FIRST + slot, 2, out));
        assert(out.deviceIndex == 1 && out.subCmd == DEV_ALERT_FIRST && out.alertSlot == slot);
    }
}

void TestSystemMoveCommands() {
    using namespace command_routing;
    int item = -1, dir = 0;

    assert(!DecodeSystemMoveCommand(ID_SYS_MOVE_FIRST - 1u, 7, item, dir));
    assert(!DecodeSystemMoveCommand(ID_SYS_MOVE_FIRST + 14u, 7, item, dir));
    assert(!DecodeSystemMoveCommand(ID_SYS_MOVE_FIRST + 16u, 10, item, dir));

    for (int i = 0; i < 7; ++i) {
        assert(DecodeSystemMoveCommand(ID_SYS_MOVE_FIRST + static_cast<unsigned int>(i) * 2u, 7, item, dir));
        assert(item == i && dir == -1);
        assert(DecodeSystemMoveCommand(ID_SYS_MOVE_FIRST + static_cast<unsigned int>(i) * 2u + 1u, 7, item, dir));
        assert(item == i && dir == +1);
    }
}

void TestIndexedCommands() {
    using namespace command_routing;
    auto dec = [](unsigned int cmd) {
        return DecodeIndexedCommand(cmd,
                                    /*positionCount*/ 6,
                                    /*chargeLimitCount*/ 3,
                                    /*eveningHourCount*/ 4,
                                    /*osdModeCount*/ 4,
                                    /*tempPresetCount*/ 3,
                                    /*tempAlertCount*/ 5,
                                    /*pollCount*/ 8,
                                    /*sysPollCount*/ 4,
                                    /*langCount*/ 20,
                                    /*monitorCount*/ 3,
                                    /*ignoredCount*/ 5);
    };

    assert(dec(ID_POSITION_FIRST).kind == IndexedRangeKind::TaskbarOffsetPreset && dec(ID_POSITION_FIRST).index == 0);
    assert(dec(ID_POSITION_FIRST + 5u).kind == IndexedRangeKind::TaskbarOffsetPreset && dec(ID_POSITION_FIRST + 5u).index == 5);
    assert(dec(ID_POSITION_FIRST + 6u).kind == IndexedRangeKind::TaskbarOffsetCustom && dec(ID_POSITION_FIRST + 6u).index == 0);
    assert(dec(ID_POSITION_FIRST + 7u).kind == IndexedRangeKind::None);

    assert(dec(ID_LIMIT_FIRST).kind == IndexedRangeKind::ChargeLimit && dec(ID_LIMIT_FIRST).index == 0);
    assert(dec(ID_LIMIT_FIRST + 2u).kind == IndexedRangeKind::ChargeLimit && dec(ID_LIMIT_FIRST + 2u).index == 2);
    assert(dec(ID_LIMIT_FIRST + 3u).kind == IndexedRangeKind::None);

    assert(dec(ID_EVENING_REMINDER_FIRST).kind == IndexedRangeKind::EveningReminder);
    assert(dec(ID_EVENING_REMINDER_FIRST + 3u).index == 3);
    assert(dec(ID_EVENING_REMINDER_FIRST + 4u).kind == IndexedRangeKind::None);

    assert(dec(ID_ALERT_SOUND_FIRST).kind == IndexedRangeKind::AlertSound && dec(ID_ALERT_SOUND_FIRST + 1u).index == 1);
    assert(dec(ID_ALERT_SOUND_FIRST + 2u).kind == IndexedRangeKind::None);

    assert(dec(ID_OSD_MODE_FIRST).kind == IndexedRangeKind::OsdMode && dec(ID_OSD_MODE_FIRST + 3u).index == 3);
    assert(dec(ID_OSD_MODE_FIRST + 4u).kind == IndexedRangeKind::None);

    assert(dec(ID_OSD_CORNER_FIRST).kind == IndexedRangeKind::OsdCorner && dec(ID_OSD_CORNER_FIRST + 3u).index == 3);
    assert(dec(ID_OSD_CORNER_FIRST + 4u).kind == IndexedRangeKind::None);

    assert(dec(ID_TEMP_PRESET_FIRST).kind == IndexedRangeKind::TempPreset && dec(ID_TEMP_PRESET_FIRST + 2u).index == 2);
    assert(dec(ID_TEMP_PRESET_FIRST + 3u).kind == IndexedRangeKind::None);

    assert(dec(ID_TEMP_ALERT_FIRST).kind == IndexedRangeKind::TempAlert && dec(ID_TEMP_ALERT_FIRST + 4u).index == 4);
    assert(dec(ID_TEMP_ALERT_FIRST + 5u).kind == IndexedRangeKind::None);

    assert(dec(ID_POLL_FIRST).kind == IndexedRangeKind::PollBattery && dec(ID_POLL_FIRST + 7u).index == 7);
    assert(dec(ID_POLL_FIRST + 8u).kind == IndexedRangeKind::None);

    assert(dec(ID_SYS_POLL_FIRST).kind == IndexedRangeKind::PollSystem && dec(ID_SYS_POLL_FIRST + 3u).index == 3);
    assert(dec(ID_SYS_POLL_FIRST + 4u).kind == IndexedRangeKind::None);

    assert(dec(ID_LANG_FIRST).kind == IndexedRangeKind::Language && dec(ID_LANG_FIRST + 19u).index == 19);
    assert(dec(ID_LANG_FIRST + 20u).kind == IndexedRangeKind::None);

    assert(dec(ID_MONITOR_FIRST).kind == IndexedRangeKind::Monitor && dec(ID_MONITOR_FIRST + 2u).index == 2);
    assert(dec(ID_MONITOR_FIRST + 3u).kind == IndexedRangeKind::None);

    assert(dec(ID_UNIGNORE_FIRST).kind == IndexedRangeKind::Unignore && dec(ID_UNIGNORE_FIRST + 4u).index == 4);
    assert(dec(ID_UNIGNORE_FIRST + 5u).kind == IndexedRangeKind::None);

    assert(dec(ID_REFRESH).kind == IndexedRangeKind::None);
}

}  // namespace

int main() {
    TestIdRangesDoNotOverlap();
    TestDeviceCommands();
    TestSystemMoveCommands();
    TestIndexedCommands();
    std::puts("Command routing tests passed.");
    return 0;
}
