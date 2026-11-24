#pragma once
// Pure menu command IDs and range decoders shared between Win32 menu/panel routing and unit tests.
// Free of <windows.h> so command decoding can be verified standalone.
#include "alert_rules.h"

#include <cstddef>
#include <cstdint>

namespace command_routing {

enum MenuId : unsigned int {
    ID_REFRESH = 100, ID_AUTOSTART, ID_HIDE_LAST_KNOWN, ID_WIDGET_BACKGROUND, ID_EXIT,
    ID_MODE_WIDGET, ID_MODE_TRAY, ID_MODE_MYFINDER, ID_SUPPORT, ID_REPORT,
    ID_BRAND_COLORS, ID_SYSTEM_COLORS, ID_STACKED, ID_TWO_ROWS, ID_CUSTOM_ICONS,
    ID_SHOW_DISCONNECTED, ID_KEEP_MOUSE_LAST_KNOWN, ID_RECORD_HISTORY, ID_DIM_SLEEPING,
    ID_TRAY_WHEN_EMPTY, ID_MAKE_ROOM,
    ID_BAT_LEFT, ID_BAT_BEFORE, ID_BAT_CLOCK,
    ID_VIT_LEFT, ID_VIT_BEFORE, ID_VIT_CLOCK,
    ID_BATTERIES_FIRST, ID_VITALS_FIRST,
    ID_SHOW_CPU, ID_SHOW_RAM, ID_SHOW_GPU, ID_SHOW_GPU_TEMP, ID_SHOW_GPU_CLOCKS, ID_SHOW_VRAM,
    ID_SHOW_CPU_W, ID_SHOW_GPU_W, ID_SHOW_CPU_TEMP, ID_REMOVE_TEMP,
    ID_SHOW_DISK_TEMP, ID_SHOW_DISK_USE, ID_SHOW_NET_SPEED, ID_RUN_SPEED_TEST,
    ID_SHOW_LAPTOP_BAT, ID_SHOW_LAPTOP_BAT_W, ID_SYSTEM_DASHBOARD,
    ID_DASHBOARD, ID_CHARGED_ALERT, ID_TIERED_ALERTS, ID_SHORT_RUNTIME_ALERT,
    ID_TRACK_SINCE, ID_TRACK_AWAKE, ID_TRACK_EMPTY, ID_RUNTIME_WIDGET,
    ID_PERCENT_SIGN, ID_RING_STYLE, ID_ANIMATE_CHARGING, ID_CONNECTION_ICONS,
    ID_HOVER_CARD, ID_HOVER_CHART, ID_TRAY_DEVICE_ICONS,
    ID_PLACE_AUTO, ID_PLACE_TOP, ID_PLACE_TRAY, ID_PLACE_BOTTOM,  // "Position on a vertical taskbar"
    ID_SYS_MOVE_FIRST = 750,  // System strip item order (up to 8 items, 2 ids each)
    ID_POSITION_FIRST = 700,  // "Position on the taskbar": ID_POSITION_FIRST + 0..5, then the custom value
    ID_POLL_FIRST = 500,      // "Check battery every": ID_POLL_FIRST + 0..7
    ID_SYS_POLL_FIRST = 550,  // "Check system every": ID_SYS_POLL_FIRST + 0..3
    ID_LIMIT_OFF = 600, ID_LIMIT_FIRST,  // "Notify when charged to": off, then alerts::kChargeLimits
    ID_EVENING_REMINDER_OFF = 610, ID_EVENING_REMINDER_FIRST,  // "Evening charge reminder": Off, 20:00..23:00
    ID_OSD_MODE_FIRST = 620,    // "In-game alerts": Off, Only in full screen, Full screen (game monitor), Always
    ID_OSD_CORNER_FIRST = 630,  // "In-game alert corner": Top right, Top left, Bottom right, Bottom left
    ID_ALERT_SOUND_FIRST = 640, // "Alert sound": Off, Windows default
    ID_TEMP_ALERT_OFF = 650, ID_TEMP_ALERT_MATCH, ID_TEMP_ALERT_FIRST,  // "High temperature alert": off, match preset, then kTempAlertOptions
    ID_TEMP_PRESET_FIRST = 670,  // "Temperature colours": Standard, Laptop, Strict
    ID_POPUP_CHARGE_LIMIT = 800, ID_POPUP_POLL_BATTERY, ID_POPUP_OSD_MODE, ID_POPUP_OSD_CORNER,
    ID_POPUP_TEMP_COLORS, ID_POPUP_TEMP_ALERT, ID_POPUP_POLL_SYSTEM,
    ID_POPUP_BAT_POS, ID_POPUP_VIT_POS, ID_POPUP_STRIP_ORDER, ID_POPUP_TASKBAR_POS,
    ID_POPUP_VERTICAL_POS, ID_POPUP_MONITORS, ID_POPUP_LANG, ID_POPUP_TRACKING, ID_POPUP_UNSUPPORTED,
    ID_POPUP_IGNORED, ID_POPUP_ALERT_SOUND, ID_POPUP_EVENING_REMINDER,
    ID_UNIGNORE_FIRST = 900,  // "Ignored devices...": up to 64 entries (900..963)
    ID_UPDATE = 200, ID_AUTO_UPDATE, ID_EXPORT_SETTINGS, ID_IMPORT_SETTINGS, ID_POLL_LESS_FULLSCREEN, ID_LOCAL_API_PIPE,
    ID_LANG_AUTO = 400, ID_LANG_ADD, ID_LANG_FIRST,  // "Language": ID_LANG_FIRST + 0..63
    ID_MONITOR_ALL = 300, ID_MONITOR_MAIN, ID_MONITOR_FIRST,  // "Show the widget on": ID_MONITOR_FIRST + 0..31
    ID_DEVICE_FIRST = 1000
};

enum DeviceCmd : unsigned int {
    DEV_TOGGLE, DEV_LEFT, DEV_RIGHT, DEV_RENAME, DEV_RESET_NAME, DEV_HISTORY,
    DEV_ALERT_FIRST = 6, DEV_ALERT_DEFAULT = DEV_ALERT_FIRST + 8, DEV_ALERT_OFF,
    DEV_MORE_POPUP = 16,
    DEV_DASHBOARD = 17,
    DEV_IGNORE = 18,
    DEV_STRIDE = 32
};

struct DecodedDeviceCmd {
    size_t deviceIndex = 0;
    DeviceCmd subCmd = DEV_TOGGLE;
    int alertSlot = -1;
};

inline bool DecodeDeviceCommand(unsigned int cmd, size_t deviceCount, DecodedDeviceCmd& out) {
    if (cmd < ID_DEVICE_FIRST || deviceCount == 0) return false;
    const unsigned int rel = cmd - ID_DEVICE_FIRST;
    const size_t idx = static_cast<size_t>(rel / DEV_STRIDE);
    if (idx >= deviceCount) return false;
    const unsigned int slot = rel % DEV_STRIDE;
    out = DecodedDeviceCmd{};
    out.deviceIndex = idx;
    if (slot == DEV_TOGGLE || slot == DEV_LEFT || slot == DEV_RIGHT || slot == DEV_RENAME ||
        slot == DEV_RESET_NAME || slot == DEV_HISTORY || slot == DEV_ALERT_DEFAULT ||
        slot == DEV_ALERT_OFF || slot == DEV_MORE_POPUP || slot == DEV_DASHBOARD || slot == DEV_IGNORE) {
        out.subCmd = static_cast<DeviceCmd>(slot);
        return true;
    }
    if (slot >= DEV_ALERT_FIRST && slot < DEV_ALERT_FIRST + static_cast<unsigned int>(alerts::kThresholdCount)) {
        out.subCmd = DEV_ALERT_FIRST;
        out.alertSlot = static_cast<int>(slot - DEV_ALERT_FIRST);
        return true;
    }
    return false;
}

inline bool DecodeSystemMoveCommand(unsigned int cmd, size_t itemCount, int& item, int& dir) {
    if (cmd < ID_SYS_MOVE_FIRST) return false;
    const unsigned int offset = cmd - ID_SYS_MOVE_FIRST;
    const size_t idx = static_cast<size_t>(offset / 2u);
    if (idx >= itemCount || idx >= 8u) return false;
    item = static_cast<int>(idx);
    dir = (offset % 2u) ? 1 : -1;
    return true;
}

enum class IndexedRangeKind {
    None,
    TaskbarOffsetPreset,
    TaskbarOffsetCustom,
    ChargeLimit,
    EveningReminder,
    AlertSound,
    OsdMode,
    OsdCorner,
    TempPreset,
    TempAlert,
    PollBattery,
    PollSystem,
    Language,
    Monitor,
    Unignore,
};

struct IndexedCommand {
    IndexedRangeKind kind = IndexedRangeKind::None;
    size_t index = 0;
};

inline IndexedCommand DecodeIndexedCommand(unsigned int cmd,
                                           size_t positionCount,
                                           size_t chargeLimitCount,
                                           size_t eveningHourCount,
                                           size_t osdModeCount,
                                           size_t tempPresetCount,
                                           size_t tempAlertCount,
                                           size_t pollCount,
                                           size_t sysPollCount,
                                           size_t langCount,
                                           size_t monitorCount,
                                           size_t ignoredCount) {
    auto matchRange = [&](unsigned int base, size_t count, IndexedRangeKind kind, IndexedCommand& res) {
        if (count > 0 && cmd >= base && cmd < base + count) {
            res = { kind, static_cast<size_t>(cmd - base) };
            return true;
        }
        return false;
    };

    IndexedCommand out;
    if (matchRange(ID_POSITION_FIRST, positionCount, IndexedRangeKind::TaskbarOffsetPreset, out)) return out;
    if (cmd == ID_POSITION_FIRST + positionCount) return { IndexedRangeKind::TaskbarOffsetCustom, 0 };
    if (matchRange(ID_LIMIT_FIRST, chargeLimitCount, IndexedRangeKind::ChargeLimit, out)) return out;
    if (matchRange(ID_EVENING_REMINDER_FIRST, eveningHourCount, IndexedRangeKind::EveningReminder, out)) return out;
    if (matchRange(ID_ALERT_SOUND_FIRST, 2u, IndexedRangeKind::AlertSound, out)) return out;
    if (matchRange(ID_OSD_MODE_FIRST, osdModeCount, IndexedRangeKind::OsdMode, out)) return out;
    if (matchRange(ID_OSD_CORNER_FIRST, 4u, IndexedRangeKind::OsdCorner, out)) return out;
    if (matchRange(ID_TEMP_PRESET_FIRST, tempPresetCount, IndexedRangeKind::TempPreset, out)) return out;
    if (matchRange(ID_TEMP_ALERT_FIRST, tempAlertCount, IndexedRangeKind::TempAlert, out)) return out;
    if (matchRange(ID_POLL_FIRST, pollCount, IndexedRangeKind::PollBattery, out)) return out;
    if (matchRange(ID_SYS_POLL_FIRST, sysPollCount, IndexedRangeKind::PollSystem, out)) return out;
    if (matchRange(ID_LANG_FIRST, langCount, IndexedRangeKind::Language, out)) return out;
    if (matchRange(ID_MONITOR_FIRST, monitorCount, IndexedRangeKind::Monitor, out)) return out;
    if (matchRange(ID_UNIGNORE_FIRST, ignoredCount, IndexedRangeKind::Unignore, out)) return out;
    return {};
}

}  // namespace command_routing
