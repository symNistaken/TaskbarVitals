#pragma once
// Portable, Windows-independent settings JSON export/import validation and battery history CSV formatting.
#include "alert_rules.h"
#include "device_aliases.h"
#include "history_model.h"
#include "evening_reminder.h"
#include "ignore_devices.h"
#include "json_output.h"
#include "osd_policy.h"
#include "platform_compat.h"
#include "sysmon.h"
#include "system_order.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace settings_port {

constexpr size_t kMaxSettingsFileBytes = 512 * 1024;
constexpr unsigned kMaxJsonDepth = 8;
constexpr size_t kMaxJsonNodes = 16384;
constexpr size_t kSystemItemCount = 7;
constexpr int kMinTaskbarOffset = -4000;
constexpr int kMaxTaskbarOffset = 4000;
constexpr int kMinPollSeconds = 30;
constexpr int kMaxPollSeconds = 86400;
constexpr size_t kMaxHiddenDevices = 256;
constexpr size_t kMaxDeviceOrder = 64;
constexpr size_t kMaxDeviceKeyChars = 256;
constexpr size_t kMaxAlertOverrides = 256;
constexpr size_t kMaxIgnoredDevices = 256;
constexpr size_t kMaxMonitorChars = 511;
constexpr size_t kMaxLanguageChars = 63;

struct SettingsSnapshot {
    // Booleans
    bool taskbarWidget = true;
    bool myFinderWidget = false;
    bool hideLastKnownDevices = false;
    bool keepMouseLastKnown = false;
    bool moveToTrayWhenEmpty = false;
    bool showDisconnectedDevices = false;
    bool dimSleepingDevices = true;
    bool recordBatteryHistory = true;
    bool chargedAlert = true;
    bool tieredAlerts = true;
    bool shortRuntimeAlert = true;
    bool hoverCard = true;
    bool hoverCardChart = true;
    bool animateCharging = false;
    bool trayDeviceIcons = false;
    bool runtimeWidget = false;
    bool twoRowsLayout = false;
    bool customVectorIcons = true;
    bool showPercentSign = true;
    bool ringStyle = false;
    bool connectionIcons = false;
    bool makeRoomForOtherWidgets = true;
    bool widgetBackground = false;
    bool stackedLayout = false;
    bool widgetFarLeft = false;
    bool widgetBeforeApps = false;
    bool vitalsFarLeft = false;
    bool vitalsBeforeApps = false;
    bool vitalsFirst = false;
    bool showCpu = false;
    bool showRam = false;
    bool showGpu = false;
    bool showGpuTemp = false;
    bool showGpuClocks = false;
    bool showCpuWatts = false;
    bool showGpuWatts = false;
    bool showCpuTemp = false;
    bool showVram = false;
    bool showDiskTemp = false;
    bool showDiskUse = false;
    bool showLaptopBattery = false;
    bool showLaptopBatWatts = false;
    bool showNetwork = false;
    bool brandColors = false;
    bool systemColors = false;
    bool autoCheckUpdates = true;
    bool pollLessFullscreen = true;
    bool localApiPipe = false;

    // Numbers / enums / ordering
    int taskbarOffset = 0;
    int verticalPlace = 0;
    int pollIntervalSeconds = 60;
    int systemPollSeconds = 2;
    int tempColorPreset = 0;
    int tempAlertCelsius = 0;
    int chargeLimitAlert = 0;
    int inGameAlerts = 1;
    int inGameAlertCorner = 0;
    int alertSound = 0;
    int eveningReminderHour = 0;
    std::vector<int> systemOrder{ 0, 1, 2, 3, 4, 5, 6 };

    // Strings & collections
    std::wstring widgetMonitor = L"all";
    std::wstring language;
    std::wstring ignoreDevices;
    std::set<std::wstring> hiddenDevices;
    std::vector<std::wstring> deviceOrder;
    aliases::AliasMap deviceAliases;
    std::map<std::wstring, int> alertThresholds;
};

struct SettingsPresence {
    bool taskbarWidget = false;
    bool myFinderWidget = false;
    bool hideLastKnownDevices = false;
    bool keepMouseLastKnown = false;
    bool moveToTrayWhenEmpty = false;
    bool showDisconnectedDevices = false;
    bool dimSleepingDevices = false;
    bool recordBatteryHistory = false;
    bool chargedAlert = false;
    bool tieredAlerts = false;
    bool shortRuntimeAlert = false;
    bool hoverCard = false;
    bool hoverCardChart = false;
    bool animateCharging = false;
    bool trayDeviceIcons = false;
    bool runtimeWidget = false;
    bool twoRowsLayout = false;
    bool customVectorIcons = false;
    bool showPercentSign = false;
    bool ringStyle = false;
    bool connectionIcons = false;
    bool makeRoomForOtherWidgets = false;
    bool widgetBackground = false;
    bool stackedLayout = false;
    bool widgetFarLeft = false;
    bool widgetBeforeApps = false;
    bool vitalsFarLeft = false;
    bool vitalsBeforeApps = false;
    bool vitalsFirst = false;
    bool showCpu = false;
    bool showRam = false;
    bool showGpu = false;
    bool showGpuTemp = false;
    bool showGpuClocks = false;
    bool showCpuWatts = false;
    bool showGpuWatts = false;
    bool showCpuTemp = false;
    bool showVram = false;
    bool showDiskTemp = false;
    bool showDiskUse = false;
    bool showLaptopBattery = false;
    bool showLaptopBatWatts = false;
    bool showNetwork = false;
    bool brandColors = false;
    bool systemColors = false;
    bool autoCheckUpdates = false;
    bool pollLessFullscreen = false;
    bool localApiPipe = false;

    bool taskbarOffset = false;
    bool verticalPlace = false;
    bool pollIntervalSeconds = false;
    bool systemPollSeconds = false;
    bool tempColorPreset = false;
    bool tempAlertCelsius = false;
    bool chargeLimitAlert = false;
    bool inGameAlerts = false;
    bool inGameAlertCorner = false;
    bool alertSound = false;
    bool eveningReminderHour = false;
    bool systemOrder = false;

    bool widgetMonitor = false;
    bool language = false;
    bool ignoreDevices = false;
    bool hiddenDevices = false;
    bool deviceOrder = false;
    bool deviceAliases = false;
    bool alertThresholds = false;
};

struct ParsedSettings {
    SettingsSnapshot values;
    SettingsPresence has;
    size_t acceptedCount = 0;
    size_t ignoredUnknown = 0;
    size_t rejectedFields = 0;
};

namespace detail {

struct BoolFieldDesc {
    const char* key;
    bool SettingsSnapshot::* val;
    bool SettingsPresence::* has;
};

inline const std::array<BoolFieldDesc, 48>& BoolFields() {
    static const std::array<BoolFieldDesc, 48> kFields = {{
        { "TaskbarWidget", &SettingsSnapshot::taskbarWidget, &SettingsPresence::taskbarWidget },
        { "MyFinderWidget", &SettingsSnapshot::myFinderWidget, &SettingsPresence::myFinderWidget },
        { "HideLastKnownDevices", &SettingsSnapshot::hideLastKnownDevices, &SettingsPresence::hideLastKnownDevices },
        { "KeepMouseLastKnown", &SettingsSnapshot::keepMouseLastKnown, &SettingsPresence::keepMouseLastKnown },
        { "MoveToTrayWhenEmpty", &SettingsSnapshot::moveToTrayWhenEmpty, &SettingsPresence::moveToTrayWhenEmpty },
        { "ShowDisconnectedDevices", &SettingsSnapshot::showDisconnectedDevices, &SettingsPresence::showDisconnectedDevices },
        { "DimSleepingDevices", &SettingsSnapshot::dimSleepingDevices, &SettingsPresence::dimSleepingDevices },
        { "RecordBatteryHistory", &SettingsSnapshot::recordBatteryHistory, &SettingsPresence::recordBatteryHistory },
        { "ChargedAlert", &SettingsSnapshot::chargedAlert, &SettingsPresence::chargedAlert },
        { "TieredAlerts", &SettingsSnapshot::tieredAlerts, &SettingsPresence::tieredAlerts },
        { "ShortRuntimeAlert", &SettingsSnapshot::shortRuntimeAlert, &SettingsPresence::shortRuntimeAlert },
        { "HoverCard", &SettingsSnapshot::hoverCard, &SettingsPresence::hoverCard },
        { "HoverCardChart", &SettingsSnapshot::hoverCardChart, &SettingsPresence::hoverCardChart },
        { "AnimateCharging", &SettingsSnapshot::animateCharging, &SettingsPresence::animateCharging },
        { "TrayDeviceIcons", &SettingsSnapshot::trayDeviceIcons, &SettingsPresence::trayDeviceIcons },
        { "EstimatedRuntimeInWidget", &SettingsSnapshot::runtimeWidget, &SettingsPresence::runtimeWidget },
        { "TwoRowsLayout", &SettingsSnapshot::twoRowsLayout, &SettingsPresence::twoRowsLayout },
        { "CustomVectorIcons", &SettingsSnapshot::customVectorIcons, &SettingsPresence::customVectorIcons },
        { "ShowPercentSign", &SettingsSnapshot::showPercentSign, &SettingsPresence::showPercentSign },
        { "RingStyle", &SettingsSnapshot::ringStyle, &SettingsPresence::ringStyle },
        { "ConnectionIcons", &SettingsSnapshot::connectionIcons, &SettingsPresence::connectionIcons },
        { "MakeRoomForOtherWidgets", &SettingsSnapshot::makeRoomForOtherWidgets, &SettingsPresence::makeRoomForOtherWidgets },
        { "WidgetBackground", &SettingsSnapshot::widgetBackground, &SettingsPresence::widgetBackground },
        { "StackedLayout", &SettingsSnapshot::stackedLayout, &SettingsPresence::stackedLayout },
        { "WidgetFarLeft", &SettingsSnapshot::widgetFarLeft, &SettingsPresence::widgetFarLeft },
        { "WidgetBeforeApps", &SettingsSnapshot::widgetBeforeApps, &SettingsPresence::widgetBeforeApps },
        { "VitalsFarLeft", &SettingsSnapshot::vitalsFarLeft, &SettingsPresence::vitalsFarLeft },
        { "VitalsBeforeApps", &SettingsSnapshot::vitalsBeforeApps, &SettingsPresence::vitalsBeforeApps },
        { "VitalsFirst", &SettingsSnapshot::vitalsFirst, &SettingsPresence::vitalsFirst },
        { "ShowCpu", &SettingsSnapshot::showCpu, &SettingsPresence::showCpu },
        { "ShowRam", &SettingsSnapshot::showRam, &SettingsPresence::showRam },
        { "ShowGpu", &SettingsSnapshot::showGpu, &SettingsPresence::showGpu },
        { "ShowGpuTemp", &SettingsSnapshot::showGpuTemp, &SettingsPresence::showGpuTemp },
        { "ShowGpuClocks", &SettingsSnapshot::showGpuClocks, &SettingsPresence::showGpuClocks },
        { "ShowCpuWatts", &SettingsSnapshot::showCpuWatts, &SettingsPresence::showCpuWatts },
        { "ShowGpuWatts", &SettingsSnapshot::showGpuWatts, &SettingsPresence::showGpuWatts },
        { "ShowCpuTemp", &SettingsSnapshot::showCpuTemp, &SettingsPresence::showCpuTemp },
        { "ShowVram", &SettingsSnapshot::showVram, &SettingsPresence::showVram },
        { "ShowDiskTemp", &SettingsSnapshot::showDiskTemp, &SettingsPresence::showDiskTemp },
        { "ShowDiskUse", &SettingsSnapshot::showDiskUse, &SettingsPresence::showDiskUse },
        { "ShowLaptopBattery", &SettingsSnapshot::showLaptopBattery, &SettingsPresence::showLaptopBattery },
        { "ShowLaptopBatWatts", &SettingsSnapshot::showLaptopBatWatts, &SettingsPresence::showLaptopBatWatts },
        { "ShowNetwork", &SettingsSnapshot::showNetwork, &SettingsPresence::showNetwork },
        { "BrandColors", &SettingsSnapshot::brandColors, &SettingsPresence::brandColors },
        { "SystemColors", &SettingsSnapshot::systemColors, &SettingsPresence::systemColors },
        { "AutoCheckUpdates", &SettingsSnapshot::autoCheckUpdates, &SettingsPresence::autoCheckUpdates },
        { "PollLessFullscreen", &SettingsSnapshot::pollLessFullscreen, &SettingsPresence::pollLessFullscreen },
        { "LocalApiPipe", &SettingsSnapshot::localApiPipe, &SettingsPresence::localApiPipe },
    }};
    return kFields;
}

inline bool NoControlChars(const std::wstring& s) {
    for (wchar_t c : s) {
        if (static_cast<unsigned int>(c) < 32u || c == 127u) return false;
    }
    return true;
}

inline bool ValidTaskbarOffset(int v) {
    return v >= kMinTaskbarOffset && v <= kMaxTaskbarOffset;
}

inline bool ValidVerticalPlace(int v) {
    return v >= 0 && v <= 3;
}

inline bool ValidPollSeconds(int v) {
    return v >= kMinPollSeconds && v <= kMaxPollSeconds;
}

inline bool ValidSystemPollSeconds(int v) {
    return v == 1 || v == 2 || v == 5 || v == 10;
}

inline bool ValidTempPreset(int v) {
    return v >= 0 && v <= 2;
}

inline bool ValidTempAlertCelsius(int v) {
    if (v == 0 || v == sysmon::kTempAlertMatchPreset) return true;
    for (int opt : sysmon::kTempAlertOptions)
        if (v == opt) return true;
    return false;
}

inline bool ValidChargeLimit(int v) {
    if (v == 0) return true;
    for (int opt : alerts::kChargeLimits)
        if (v == opt) return true;
    return false;
}

inline bool ValidOsdMode(int v) {
    return v >= 0 && v <= 3;
}

inline bool ValidOsdCorner(int v) {
    return v >= 0 && v <= 3;
}

inline bool ValidAlertSound(int v) {
    return v == 0 || v == 1;
}

inline bool ValidEveningReminderHour(int v) {
    if (v == 0) return true;
    for (int h : evening_reminder::kHours)
        if (v == h) return true;
    return false;
}

inline bool ParseIgnoreToken(const std::wstring& token, ignore_devices::VidPid& out) {
    const size_t colon = token.find(L':');
    if (colon == std::wstring::npos || token.find(L':', colon + 1) != std::wstring::npos) return false;
    std::uint16_t vid = 0, pid = 0;
    if (!ignore_devices::detail::ParseHex16(token.substr(0, colon), vid)) return false;
    if (!ignore_devices::detail::ParseHex16(token.substr(colon + 1), pid)) return false;
    if (vid == 0 && pid == 0) return false;
    out = { vid, pid };
    return true;
}

inline std::string JsonQuoted(const std::string& utf8) {
    return "\"" + JsonEscape(utf8) + "\"";
}

inline std::string JsonQuotedWide(const std::wstring& w) {
    return JsonQuoted(WideToUtf8(w));
}

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolVal = false;
    bool isInt = false;
    int64_t intVal = 0;
    std::string str;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : input_(text) {}

    bool Parse(JsonValue& root) {
        if (input_.size() > kMaxSettingsFileBytes) return false;
        if (!ParseValue(root, 0)) return false;
        SkipSpace();
        return pos_ == input_.size();
    }

private:
    const std::string& input_;
    size_t pos_ = 0;
    size_t nodes_ = 0;

    void SkipSpace() {
        while (pos_ < input_.size()) {
            const char c = input_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos_;
            else break;
        }
    }

    bool Take(char c) {
        if (pos_ < input_.size() && input_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ParseHex4(unsigned& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos_ >= input_.size()) return false;
            const char c = input_[pos_++];
            unsigned d = 16;
            if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
            if (d == 16) return false;
            out = (out << 4) | d;
        }
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80u) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (!Take('"')) return false;
        while (pos_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20u) return false;
            if (c == '\\') {
                if (pos_ >= input_.size()) return false;
                const char esc = input_[pos_++];
                switch (esc) {
                    case '"': case '\\': case '/': out.push_back(esc); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned cp = 0;
                        if (!ParseHex4(cp)) return false;
                        if (cp >= 0xD800u && cp <= 0xDBFFu) {
                            unsigned low = 0;
                            if (!Take('\\') || !Take('u') || !ParseHex4(low) || low < 0xDC00u || low > 0xDFFFu)
                                return false;
                            cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
                        } else if (cp >= 0xDC00u && cp <= 0xDFFFu) {
                            return false;
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: return false;
                }
            } else if (c < 0x80u) {
                out.push_back(static_cast<char>(c));
            } else {
                const unsigned extra = (c >= 0xC2u && c <= 0xDFu) ? 1u
                                     : (c >= 0xE0u && c <= 0xEFu) ? 2u
                                     : (c >= 0xF0u && c <= 0xF4u) ? 3u : 0u;
                if (!extra || input_.size() - pos_ < extra) return false;
                unsigned cp = c & (0x7Fu >> extra);
                for (unsigned i = 0; i < extra; ++i) {
                    const unsigned char cont = static_cast<unsigned char>(input_[pos_++]);
                    if ((cont & 0xC0u) != 0x80u) return false;
                    cp = (cp << 6) | (cont & 0x3Fu);
                }
                if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu) ||
                    cp < (extra == 1u ? 0x80u : extra == 2u ? 0x800u : 0x10000u)) {
                    return false;
                }
                AppendUtf8(out, cp);
            }
        }
        return false;
    }

    bool ParseNumber(JsonValue& out) {
        const size_t start = pos_;
        const bool neg = Take('-');
        if (!Take('0')) {
            if (pos_ >= input_.size() || input_[pos_] < '1' || input_[pos_] > '9') return false;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
        }
        const size_t intEnd = pos_;
        bool hasFracOrExp = false;
        if (Take('.')) {
            hasFracOrExp = true;
            const size_t digits = pos_;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
            if (pos_ == digits) return false;
        }
        if (Take('e') || Take('E')) {
            hasFracOrExp = true;
            if (!Take('+')) Take('-');
            const size_t digits = pos_;
            while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
            if (pos_ == digits) return false;
        }
        out.type = JsonValue::Type::Number;
        out.isInt = false;
        out.intVal = 0;
        if (!hasFracOrExp) {
            uint64_t mag = 0;
            bool overflow = false;
            const uint64_t limit = neg ? (static_cast<uint64_t>(INT64_MAX) + 1ULL) : static_cast<uint64_t>(INT64_MAX);
            for (size_t i = start + (neg ? 1u : 0u); i < intEnd; ++i) {
                const uint64_t d = static_cast<uint64_t>(input_[i] - '0');
                if (mag > (limit - d) / 10ULL) {
                    overflow = true;
                    break;
                }
                mag = mag * 10ULL + d;
            }
            if (!overflow) {
                out.isInt = true;
                if (neg) {
                    out.intVal = mag == static_cast<uint64_t>(INT64_MAX) + 1ULL
                                 ? INT64_MIN
                                 : -static_cast<int64_t>(mag);
                } else {
                    out.intVal = static_cast<int64_t>(mag);
                }
            }
        }
        return true;
    }

    bool ParseValue(JsonValue& out, unsigned depth) {
        if (depth > kMaxJsonDepth || ++nodes_ > kMaxJsonNodes) return false;
        SkipSpace();
        if (pos_ >= input_.size()) return false;
        if (Take('{')) {
            out.type = JsonValue::Type::Object;
            SkipSpace();
            if (Take('}')) return true;
            std::set<std::string> seenKeys;
            do {
                SkipSpace();
                std::string key;
                if (!ParseString(key)) return false;
                if (!seenKeys.insert(key).second) return false;
                SkipSpace();
                if (!Take(':')) return false;
                JsonValue val;
                if (!ParseValue(val, depth + 1)) return false;
                out.obj.emplace_back(std::move(key), std::move(val));
                SkipSpace();
                if (Take('}')) return true;
            } while (Take(','));
            return false;
        }
        if (Take('[')) {
            out.type = JsonValue::Type::Array;
            SkipSpace();
            if (Take(']')) return true;
            do {
                JsonValue elem;
                if (!ParseValue(elem, depth + 1)) return false;
                out.arr.push_back(std::move(elem));
                SkipSpace();
                if (Take(']')) return true;
            } while (Take(','));
            return false;
        }
        if (input_[pos_] == '"') {
            out.type = JsonValue::Type::String;
            return ParseString(out.str);
        }
        for (const char* lit : { "true", "false", "null" }) {
            const size_t n = std::char_traits<char>::length(lit);
            if (input_.compare(pos_, n, lit) == 0) {
                out.type = (lit[0] == 'n') ? JsonValue::Type::Null : JsonValue::Type::Bool;
                out.boolVal = (lit[0] == 't');
                pos_ += n;
                return true;
            }
        }
        return ParseNumber(out);
    }
};

inline bool ParseSystemOrderValue(const JsonValue& v, std::vector<int>& out) {
    if (v.type == JsonValue::Type::Array) {
        if (v.arr.empty() || v.arr.size() > kSystemItemCount) return false;
        std::vector<int> items;
        items.reserve(v.arr.size());
        for (const auto& el : v.arr) {
            if (el.type != JsonValue::Type::Number || !el.isInt) return false;
            if (el.intVal < 0 || el.intVal >= static_cast<int64_t>(kSystemItemCount)) return false;
            const int x = static_cast<int>(el.intVal);
            if (std::find(items.begin(), items.end(), x) != items.end()) return false;
            items.push_back(x);
        }
        out = system_order::DecodeSystemOrder(system_order::EncodeSystemOrder(items), kSystemItemCount);
        return true;
    }
    if (v.type == JsonValue::Type::Number && v.isInt && v.intVal >= 0 && v.intVal <= 0xFFFFFFFFLL) {
        out = system_order::DecodeSystemOrder(static_cast<uint32_t>(v.intVal), kSystemItemCount);
        return true;
    }
    return false;
}

inline bool ParseIgnoreDevicesValue(const JsonValue& v, std::wstring& out) {
    std::vector<ignore_devices::VidPid> items;
    if (v.type == JsonValue::Type::String) {
        if (v.str.find('\0') != std::string::npos || v.str.size() > 65536) return false;
        const std::wstring raw = Utf8ToWide(v.str);
        size_t i = 0;
        while (i < raw.size()) {
            while (i < raw.size() && ignore_devices::detail::IsSeparator(raw[i])) ++i;
            if (i >= raw.size()) break;
            size_t end = i;
            while (end < raw.size() && !ignore_devices::detail::IsSeparator(raw[end])) ++end;
            ignore_devices::VidPid vp;
            if (!ParseIgnoreToken(raw.substr(i, end - i), vp)) return false;
            if (!ignore_devices::Contains(items, vp.vid, vp.pid)) {
                if (items.size() >= kMaxIgnoredDevices) return false;
                items.push_back(vp);
            }
            i = end;
        }
        out = ignore_devices::FormatList(items);
        return true;
    }
    if (v.type == JsonValue::Type::Array) {
        if (v.arr.size() > kMaxIgnoredDevices) return false;
        for (const auto& el : v.arr) {
            if (el.type != JsonValue::Type::String || el.str.find('\0') != std::string::npos) return false;
            ignore_devices::VidPid vp;
            if (!ParseIgnoreToken(Utf8ToWide(el.str), vp)) return false;
            if (!ignore_devices::Contains(items, vp.vid, vp.pid)) items.push_back(vp);
        }
        out = ignore_devices::FormatList(items);
        return true;
    }
    return false;
}

inline bool ParseStringSetValue(const JsonValue& v, size_t maxItems, std::set<std::wstring>& out) {
    if (v.type != JsonValue::Type::Array) return false;
    for (const auto& el : v.arr) {
        if (el.type != JsonValue::Type::String) return false;
    }
    out.clear();
    for (const auto& el : v.arr) {
        if (out.size() >= maxItems) break;
        if (el.str.find('\0') != std::string::npos) continue;
        std::wstring w = Utf8ToWide(el.str);
        if (!w.empty() && w.size() <= kMaxDeviceKeyChars && NoControlChars(w)) {
            out.insert(std::move(w));
        }
    }
    return true;
}

inline bool ParseStringOrderValue(const JsonValue& v, size_t maxItems, std::vector<std::wstring>& out) {
    if (v.type != JsonValue::Type::Array) return false;
    for (const auto& el : v.arr) {
        if (el.type != JsonValue::Type::String) return false;
    }
    out.clear();
    for (const auto& el : v.arr) {
        if (out.size() >= maxItems) break;
        if (el.str.find('\0') != std::string::npos) continue;
        std::wstring w = Utf8ToWide(el.str);
        if (!w.empty() && w.size() <= kMaxDeviceKeyChars && NoControlChars(w) &&
            std::find(out.begin(), out.end(), w) == out.end()) {
            out.push_back(std::move(w));
        }
    }
    return true;
}

inline bool ParseAliasesValue(const JsonValue& v, aliases::AliasMap& out) {
    if (v.type != JsonValue::Type::Object) return false;
    out.clear();
    for (const auto& [k, val] : v.obj) {
        if (out.size() >= aliases::kMaxAliases) break;
        if (val.type != JsonValue::Type::String) continue;
        if (k.find('\0') != std::string::npos || val.str.find('\0') != std::string::npos) continue;
        std::wstring key = Utf8ToWide(k);
        std::wstring name = aliases::Trim(Utf8ToWide(val.str));
        if (!key.empty() && key.size() <= aliases::kMaxDeviceKeyLength && NoControlChars(key) &&
            aliases::ValidName(name)) {
            out.emplace(std::move(key), std::move(name));
        }
    }
    return true;
}

inline bool ParseAlertThresholdsValue(const JsonValue& v, std::map<std::wstring, int>& out) {
    if (v.type != JsonValue::Type::Object) return false;
    out.clear();
    for (const auto& [k, val] : v.obj) {
        if (out.size() >= kMaxAlertOverrides) break;
        if (val.type != JsonValue::Type::Number || !val.isInt) continue;
        if (val.intVal < alerts::kDisabled || val.intVal > 40) continue;
        const int threshold = static_cast<int>(val.intVal);
        if (!alerts::ValidOverride(threshold)) continue;
        if (k.find('\0') != std::string::npos) continue;
        std::wstring key = Utf8ToWide(k);
        if (!key.empty() && key.size() <= aliases::kMaxDeviceKeyLength && NoControlChars(key)) {
            out.emplace(std::move(key), threshold);
        }
    }
    return true;
}

inline std::string Pad2(int v) {
    std::string s(2, '0');
    s[0] = static_cast<char>('0' + (v / 10) % 10);
    s[1] = static_cast<char>('0' + v % 10);
    return s;
}

inline std::string PadYear(int64_t y) {
    const bool neg = y < 0;
    const uint64_t absY = neg ? static_cast<uint64_t>(-y) : static_cast<uint64_t>(y);
    std::string digits = std::to_string(absY);
    if (digits.size() < 4) digits.insert(0, 4 - digits.size(), '0');
    return neg ? ("-" + digits) : digits;
}

}  // namespace detail

inline std::string SettingsToJson(const SettingsSnapshot& s) {
    std::vector<std::string> lines;
    lines.reserve(56);

    for (const auto& desc : detail::BoolFields()) {
        lines.push_back("  " + detail::JsonQuoted(desc.key) + ": " + ((s.*(desc.val)) ? "true" : "false"));
    }

    lines.push_back("  \"TaskbarOffset\": " + std::to_string(s.taskbarOffset));
    lines.push_back("  \"VerticalPlace\": " + std::to_string(s.verticalPlace));
    lines.push_back("  \"PollIntervalSeconds\": " + std::to_string(s.pollIntervalSeconds));
    lines.push_back("  \"SystemPollSeconds\": " + std::to_string(s.systemPollSeconds));
    lines.push_back("  \"TempColorPreset\": " + std::to_string(s.tempColorPreset));
    lines.push_back("  \"TempAlertCelsius\": " + std::to_string(s.tempAlertCelsius));
    lines.push_back("  \"ChargeLimitAlert\": " + std::to_string(s.chargeLimitAlert));
    lines.push_back("  \"InGameAlerts\": " + std::to_string(s.inGameAlerts));
    lines.push_back("  \"InGameAlertCorner\": " + std::to_string(s.inGameAlertCorner));
    lines.push_back("  \"AlertSound\": " + std::to_string(s.alertSound));
    lines.push_back("  \"EveningReminderHour\": " + std::to_string(s.eveningReminderHour));

    {
        const std::vector<int> normOrder =
            system_order::DecodeSystemOrder(system_order::EncodeSystemOrder(s.systemOrder), kSystemItemCount);
        std::string arr = "[";
        for (size_t i = 0; i < normOrder.size(); ++i) {
            if (i) arr += ", ";
            arr += std::to_string(normOrder[i]);
        }
        arr += "]";
        lines.push_back("  \"SystemOrder\": " + arr);
    }

    lines.push_back("  \"WidgetMonitor\": " + detail::JsonQuotedWide(s.widgetMonitor));
    lines.push_back("  \"Language\": " + detail::JsonQuotedWide(s.language));
    lines.push_back("  \"IgnoreDevices\": " + detail::JsonQuotedWide(s.ignoreDevices));

    auto appendStringArray = [&](const char* key, const auto& items) {
        if (items.empty()) {
            lines.push_back(std::string("  \"") + key + "\": []");
            return;
        }
        std::string block = std::string("  \"") + key + "\": [\n";
        bool first = true;
        for (const auto& item : items) {
            if (!first) block += ",\n";
            first = false;
            block += "    " + detail::JsonQuotedWide(item);
        }
        block += "\n  ]";
        lines.push_back(std::move(block));
    };

    appendStringArray("HiddenDevices", s.hiddenDevices);
    appendStringArray("DeviceOrder", s.deviceOrder);

    {
        if (s.deviceAliases.empty()) {
            lines.push_back("  \"DeviceAliases\": {}");
        } else {
            std::string block = "  \"DeviceAliases\": {\n";
            bool first = true;
            for (const auto& [k, v] : s.deviceAliases) {
                if (!first) block += ",\n";
                first = false;
                block += "    " + detail::JsonQuotedWide(k) + ": " + detail::JsonQuotedWide(v);
            }
            block += "\n  }";
            lines.push_back(std::move(block));
        }
    }

    {
        if (s.alertThresholds.empty()) {
            lines.push_back("  \"AlertThresholds\": {}");
        } else {
            std::string block = "  \"AlertThresholds\": {\n";
            bool first = true;
            for (const auto& [k, v] : s.alertThresholds) {
                if (!first) block += ",\n";
                first = false;
                block += "    " + detail::JsonQuotedWide(k) + ": " + std::to_string(v);
            }
            block += "\n  }";
            lines.push_back(std::move(block));
        }
    }

    std::string out = "{\n";
    for (size_t i = 0; i < lines.size(); ++i) {
        out += lines[i];
        out += (i + 1 < lines.size()) ? ",\n" : "\n";
    }
    out += "}\n";
    return out;
}

inline bool ParseSettingsJson(const std::string& json, ParsedSettings& out) {
    out = ParsedSettings{};
    detail::JsonValue root;
    if (!detail::JsonParser(json).Parse(root)) return false;
    if (root.type != detail::JsonValue::Type::Object) return false;

    auto acceptInt = [&](const detail::JsonValue& v, auto validFn, int& dst, bool& flag) {
        if (v.type == detail::JsonValue::Type::Number && v.isInt &&
            v.intVal >= INT32_MIN && v.intVal <= INT32_MAX && validFn(static_cast<int>(v.intVal))) {
            dst = static_cast<int>(v.intVal);
            flag = true;
            ++out.acceptedCount;
        } else {
            ++out.rejectedFields;
        }
    };

    for (const auto& [key, val] : root.obj) {
        bool matchedBool = false;
        for (const auto& desc : detail::BoolFields()) {
            if (key == desc.key) {
                matchedBool = true;
                if (val.type == detail::JsonValue::Type::Bool) {
                    out.values.*(desc.val) = val.boolVal;
                    out.has.*(desc.has) = true;
                    ++out.acceptedCount;
                } else {
                    ++out.rejectedFields;
                }
                break;
            }
        }
        if (matchedBool) continue;

        if (key == "TaskbarOffset") {
            acceptInt(val, detail::ValidTaskbarOffset, out.values.taskbarOffset, out.has.taskbarOffset);
        } else if (key == "VerticalPlace") {
            acceptInt(val, detail::ValidVerticalPlace, out.values.verticalPlace, out.has.verticalPlace);
        } else if (key == "PollIntervalSeconds") {
            acceptInt(val, detail::ValidPollSeconds, out.values.pollIntervalSeconds, out.has.pollIntervalSeconds);
        } else if (key == "SystemPollSeconds") {
            acceptInt(val, detail::ValidSystemPollSeconds, out.values.systemPollSeconds, out.has.systemPollSeconds);
        } else if (key == "TempColorPreset") {
            acceptInt(val, detail::ValidTempPreset, out.values.tempColorPreset, out.has.tempColorPreset);
        } else if (key == "TempAlertCelsius") {
            acceptInt(val, detail::ValidTempAlertCelsius, out.values.tempAlertCelsius, out.has.tempAlertCelsius);
        } else if (key == "ChargeLimitAlert") {
            acceptInt(val, detail::ValidChargeLimit, out.values.chargeLimitAlert, out.has.chargeLimitAlert);
        } else if (key == "InGameAlerts") {
            acceptInt(val, detail::ValidOsdMode, out.values.inGameAlerts, out.has.inGameAlerts);
        } else if (key == "InGameAlertCorner") {
            acceptInt(val, detail::ValidOsdCorner, out.values.inGameAlertCorner, out.has.inGameAlertCorner);
        } else if (key == "AlertSound") {
            acceptInt(val, detail::ValidAlertSound, out.values.alertSound, out.has.alertSound);
        } else if (key == "EveningReminderHour") {
            acceptInt(val, detail::ValidEveningReminderHour, out.values.eveningReminderHour, out.has.eveningReminderHour);
        } else if (key == "SystemOrder") {
            if (detail::ParseSystemOrderValue(val, out.values.systemOrder)) {
                out.has.systemOrder = true;
                ++out.acceptedCount;
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "WidgetMonitor") {
            if (val.type == detail::JsonValue::Type::String && val.str.find('\0') == std::string::npos) {
                std::wstring w = Utf8ToWide(val.str);
                if (!w.empty() && w.size() <= kMaxMonitorChars && detail::NoControlChars(w)) {
                    out.values.widgetMonitor = std::move(w);
                    out.has.widgetMonitor = true;
                    ++out.acceptedCount;
                } else {
                    ++out.rejectedFields;
                }
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "Language") {
            if (val.type == detail::JsonValue::Type::String && val.str.find('\0') == std::string::npos) {
                std::wstring w = Utf8ToWide(val.str);
                if (w.size() <= kMaxLanguageChars && detail::NoControlChars(w) &&
                    w.find(L'/') == std::wstring::npos && w.find(L'\\') == std::wstring::npos) {
                    out.values.language = std::move(w);
                    out.has.language = true;
                    ++out.acceptedCount;
                } else {
                    ++out.rejectedFields;
                }
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "IgnoreDevices") {
            if (detail::ParseIgnoreDevicesValue(val, out.values.ignoreDevices)) {
                out.has.ignoreDevices = true;
                ++out.acceptedCount;
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "HiddenDevices") {
            if (detail::ParseStringSetValue(val, kMaxHiddenDevices, out.values.hiddenDevices)) {
                out.has.hiddenDevices = true;
                ++out.acceptedCount;
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "DeviceOrder") {
            if (detail::ParseStringOrderValue(val, kMaxDeviceOrder, out.values.deviceOrder)) {
                out.has.deviceOrder = true;
                ++out.acceptedCount;
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "DeviceAliases") {
            if (detail::ParseAliasesValue(val, out.values.deviceAliases)) {
                out.has.deviceAliases = true;
                ++out.acceptedCount;
            } else {
                ++out.rejectedFields;
            }
        } else if (key == "AlertThresholds") {
            if (detail::ParseAlertThresholdsValue(val, out.values.alertThresholds)) {
                out.has.alertThresholds = true;
                ++out.acceptedCount;
            } else {
                ++out.rejectedFields;
            }
        } else {
            // Unknown keys (including any Run / Autostart / startup entries) are ignored completely.
            ++out.ignoredUnknown;
        }
    }
    return true;
}

// Howard Hinnant's civil_from_days algorithm: deterministic UTC YYYY-MM-DDTHH:MM:SSZ without gmtime/locale quirks.
inline std::string FormatUtcIso8601(int64_t timestamp) {
    int64_t days = timestamp / 86400;
    int64_t rem = timestamp % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    const int64_t z = days + 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const uint64_t doe = static_cast<uint64_t>(z - era * 146097);
    const uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint64_t mp = (5 * doy + 2) / 153;
    const uint64_t d = doy - (153 * mp + 2) / 5 + 1;
    const uint64_t m = mp < 10 ? (mp + 3) : (mp - 9);
    y += (m <= 2) ? 1 : 0;

    const int hour = static_cast<int>(rem / 3600);
    const int minute = static_cast<int>((rem % 3600) / 60);
    const int second = static_cast<int>(rem % 60);

    return detail::PadYear(y) + "-" + detail::Pad2(static_cast<int>(m)) + "-" +
           detail::Pad2(static_cast<int>(d)) + "T" + detail::Pad2(hour) + ":" +
           detail::Pad2(minute) + ":" + detail::Pad2(second) + "Z";
}

// RFC-4180 CSV field escaping.
inline std::string CsvEscape(const std::string& field) {
    if (field.find_first_of(",\"\r\n") == std::string::npos) return field;
    std::string out;
    out.reserve(field.size() + 4);
    out.push_back('"');
    for (char c : field) {
        if (c == '"') out += "\"\"";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

inline std::string HistoryToCsv(const std::wstring& deviceKey, const std::wstring& deviceName,
                                const std::vector<battery_history::Sample>& samples) {
    std::string out = "device_key,device_name,utc_iso8601,percent,charging\n";
    const std::string escapedKey = CsvEscape(WideToUtf8(deviceKey));
    const std::string escapedName = CsvEscape(WideToUtf8(deviceName));
    for (const auto& s : samples) {
        const std::string rowKey = s.key.empty() ? escapedKey : CsvEscape(WideToUtf8(s.key));
        out += rowKey + "," + escapedName + "," + FormatUtcIso8601(s.timestamp) + "," +
               std::to_string(s.percent) + "," + (s.charging ? "true" : "false") + "\n";
    }
    return out;
}

}  // namespace settings_port
