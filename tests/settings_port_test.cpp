// Portable tests for settings JSON export/import validation and battery history CSV formatting.
#include "../src/settings_port.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

void TestSettingsRoundTrip() {
    using namespace settings_port;

    SettingsSnapshot s;
    s.taskbarWidget = false;
    s.myFinderWidget = true;
    s.hideLastKnownDevices = true;
    s.keepMouseLastKnown = true;
    s.moveToTrayWhenEmpty = true;
    s.showDisconnectedDevices = true;
    s.dimSleepingDevices = false;
    s.recordBatteryHistory = false;
    s.chargedAlert = false;
    s.tieredAlerts = false;
    s.shortRuntimeAlert = false;
    s.hoverCard = false;
    s.hoverCardChart = false;
    s.animateCharging = true;
    s.trayDeviceIcons = true;
    s.runtimeWidget = true;
    s.twoRowsLayout = true;
    s.customVectorIcons = false;
    s.showPercentSign = false;
    s.ringStyle = true;
    s.connectionIcons = true;
    s.makeRoomForOtherWidgets = false;
    s.widgetBackground = true;
    s.stackedLayout = true;
    s.widgetFarLeft = true;
    s.widgetBeforeApps = true;
    s.vitalsFarLeft = true;
    s.vitalsBeforeApps = true;
    s.vitalsFirst = true;
    s.showCpu = true;
    s.showRam = true;
    s.showGpu = true;
    s.showGpuTemp = true;
    s.showGpuClocks = true;
    s.showCpuWatts = true;
    s.showGpuWatts = true;
    s.showCpuTemp = true;
    s.showVram = true;
    s.showDiskTemp = true;
    s.showDiskUse = true;
    s.showLaptopBattery = true;
    s.showLaptopBatWatts = true;
    s.showNetwork = true;
    s.brandColors = true;
    s.systemColors = true;
    s.autoCheckUpdates = false;
    s.pollLessFullscreen = false;
    s.localApiPipe = true;

    s.taskbarOffset = -120;
    s.verticalPlace = 2;
    s.pollIntervalSeconds = 300;
    s.systemPollSeconds = 5;
    s.tempColorPreset = 2;
    s.tempAlertCelsius = 85;
    s.chargeLimitAlert = 80;
    s.inGameAlerts = 2;
    s.inGameAlertCorner = 3;
    s.alertSound = 1;
    s.eveningReminderHour = 22;
    s.systemOrder = { 6, 5, 4, 3, 2, 1, 0 };

    s.widgetMonitor = L"\\\\.\\DISPLAY2";
    s.language = L"tr";
    s.ignoreDevices = L"046D:C547, 1532:0084";
    s.hiddenDevices = { L"razer:mouse1", L"bt:buds\u00e7" };
    s.deviceOrder = { L"bt:buds\u00e7", L"kysona:373B:1085" };
    s.deviceAliases[L"kysona:373B:1085"] = L"Moja \"mysz\" \U0001F3A7";
    s.deviceAliases[L"razer:mouse1"] = L"Line\\Slash";
    s.alertThresholds[L"kysona:373B:1085"] = 25;
    s.alertThresholds[L"razer:mouse1"] = -1;

    const std::string json = SettingsToJson(s);
    ParsedSettings parsed;
    assert(ParseSettingsJson(json, parsed));
    assert(parsed.ignoredUnknown == 0);
    assert(parsed.rejectedFields == 0);
    assert(parsed.acceptedCount == 67);

    const auto& v = parsed.values;
    assert(v.taskbarWidget == s.taskbarWidget);
    assert(v.myFinderWidget == s.myFinderWidget);
    assert(v.hideLastKnownDevices == s.hideLastKnownDevices);
    assert(v.keepMouseLastKnown == s.keepMouseLastKnown);
    assert(v.moveToTrayWhenEmpty == s.moveToTrayWhenEmpty);
    assert(v.showDisconnectedDevices == s.showDisconnectedDevices);
    assert(v.dimSleepingDevices == s.dimSleepingDevices);
    assert(v.recordBatteryHistory == s.recordBatteryHistory);
    assert(v.chargedAlert == s.chargedAlert);
    assert(v.tieredAlerts == s.tieredAlerts);
    assert(v.shortRuntimeAlert == s.shortRuntimeAlert);
    assert(v.hoverCard == s.hoverCard);
    assert(v.hoverCardChart == s.hoverCardChart);
    assert(v.animateCharging == s.animateCharging);
    assert(v.trayDeviceIcons == s.trayDeviceIcons);
    assert(v.runtimeWidget == s.runtimeWidget);
    assert(v.twoRowsLayout == s.twoRowsLayout);
    assert(v.customVectorIcons == s.customVectorIcons);
    assert(v.showPercentSign == s.showPercentSign);
    assert(v.ringStyle == s.ringStyle);
    assert(v.connectionIcons == s.connectionIcons);
    assert(v.makeRoomForOtherWidgets == s.makeRoomForOtherWidgets);
    assert(v.widgetBackground == s.widgetBackground);
    assert(v.stackedLayout == s.stackedLayout);
    assert(v.widgetFarLeft == s.widgetFarLeft);
    assert(v.widgetBeforeApps == s.widgetBeforeApps);
    assert(v.vitalsFarLeft == s.vitalsFarLeft);
    assert(v.vitalsBeforeApps == s.vitalsBeforeApps);
    assert(v.vitalsFirst == s.vitalsFirst);
    assert(v.showCpu == s.showCpu);
    assert(v.showRam == s.showRam);
    assert(v.showGpu == s.showGpu);
    assert(v.showGpuTemp == s.showGpuTemp);
    assert(v.showGpuClocks == s.showGpuClocks);
    assert(v.showCpuWatts == s.showCpuWatts);
    assert(v.showGpuWatts == s.showGpuWatts);
    assert(v.showCpuTemp == s.showCpuTemp);
    assert(v.showVram == s.showVram);
    assert(v.showDiskTemp == s.showDiskTemp);
    assert(v.showDiskUse == s.showDiskUse);
    assert(v.showLaptopBattery == s.showLaptopBattery);
    assert(v.showLaptopBatWatts == s.showLaptopBatWatts);
    assert(v.showNetwork == s.showNetwork);
    assert(v.brandColors == s.brandColors);
    assert(v.systemColors == s.systemColors);
    assert(v.autoCheckUpdates == s.autoCheckUpdates);
    assert(v.pollLessFullscreen == s.pollLessFullscreen);
    assert(v.localApiPipe == s.localApiPipe);
    assert(v.taskbarOffset == s.taskbarOffset);
    assert(v.verticalPlace == s.verticalPlace);
    assert(v.pollIntervalSeconds == s.pollIntervalSeconds);
    assert(v.systemPollSeconds == s.systemPollSeconds);
    assert(v.tempColorPreset == s.tempColorPreset);
    assert(v.tempAlertCelsius == s.tempAlertCelsius);
    assert(v.chargeLimitAlert == s.chargeLimitAlert);
    assert(v.inGameAlerts == s.inGameAlerts);
    assert(v.inGameAlertCorner == s.inGameAlertCorner);
    assert(v.alertSound == s.alertSound);
    assert(v.eveningReminderHour == s.eveningReminderHour);
    assert(v.systemOrder == s.systemOrder);
    assert(v.widgetMonitor == s.widgetMonitor);
    assert(v.language == s.language);
    assert(v.ignoreDevices == s.ignoreDevices);
    assert(v.hiddenDevices == s.hiddenDevices);
    assert(v.deviceOrder == s.deviceOrder);
    assert(v.deviceAliases == s.deviceAliases);
    assert(v.alertThresholds == s.alertThresholds);

    // Ensure no startup/Run or runtime-cycle keys are emitted by export.
    assert(json.find("\"Run\"") == std::string::npos);
    assert(json.find("Autostart") == std::string::npos);
    assert(json.find("StartWithWindows") == std::string::npos);
    assert(json.find("EveningReminderLastCycle") == std::string::npos);
}

void TestFilteringAndValidation() {
    using namespace settings_port;

    // Unknown keys and Run/Autostart keys ignored; partial valid JSON accepted; invalid values rejected per field.
    const std::string input = R"({
        "Run": true,
        "Autostart": "C:\\evil.exe",
        "StartWithWindows": true,
        "EveningReminderLastCycle": 20261124,
        "UnknownNested": { "a": [1, 2.5e-3, null, false, {"deep": "ok"}] },
        "BrandColors": true,
        "ShowCpu": "yes",
        "PollLessFullscreen": "no",
        "LocalApiPipe": 1,
        "TaskbarOffset": -4001,
        "VerticalPlace": 4,
        "PollIntervalSeconds": 29,
        "SystemPollSeconds": 3,
        "TempColorPreset": 9,
        "TempAlertCelsius": 77,
        "ChargeLimitAlert": 75,
        "InGameAlerts": 4,
        "InGameAlertCorner": -1,
        "AlertSound": 2,
        "EveningReminderHour": 19,
        "SystemOrder": [2, 0, 99],
        "WidgetMonitor": "",
        "Language": "../evil",
        "IgnoreDevices": "bad-vidpid",
        "HiddenDevices": [123],
        "DeviceOrder": ["dev:a", "", "dev:a", "dev:b"],
        "DeviceAliases": {
            "dev:a": "  Trimmed Name  ",
            "dev:empty": "   ",
            "dev:too_long": ")" + std::string(65, 'x') + R"(",
            "dev:badtype": 42
        },
        "AlertThresholds": {
            "dev:a": 20,
            "dev:off": -1,
            "dev:bad1": 0,
            "dev:bad2": 45,
            "dev:bad3": "15",
            "dev:bad\tkey": 10
        }
    })";

    ParsedSettings parsed;
    assert(ParseSettingsJson(input, parsed));
    assert(parsed.ignoredUnknown == 5);  // Run, Autostart, StartWithWindows, EveningReminderLastCycle, UnknownNested
    assert(parsed.has.brandColors && parsed.values.brandColors == true);

    // Rejected invalid fields keep default / not-present flags.
    assert(!parsed.has.showCpu);
    assert(!parsed.has.taskbarOffset);
    assert(!parsed.has.verticalPlace);
    assert(!parsed.has.pollIntervalSeconds);
    assert(!parsed.has.systemPollSeconds);
    assert(!parsed.has.tempColorPreset);
    assert(!parsed.has.tempAlertCelsius);
    assert(!parsed.has.chargeLimitAlert);
    assert(!parsed.has.pollLessFullscreen);
    assert(!parsed.has.localApiPipe);
    assert(!parsed.has.inGameAlerts);
    assert(!parsed.has.inGameAlertCorner);
    assert(!parsed.has.alertSound);
    assert(!parsed.has.eveningReminderHour);
    assert(!parsed.has.systemOrder);
    assert(!parsed.has.widgetMonitor);
    assert(!parsed.has.language);
    assert(!parsed.has.ignoreDevices);
    assert(!parsed.has.hiddenDevices);

    // DeviceOrder had valid strings only -> accepted, deduplicated, empty filtered.
    assert(parsed.has.deviceOrder);
    assert((parsed.values.deviceOrder == std::vector<std::wstring>{ L"dev:a", L"dev:b" }));

    // DeviceAliases trimmed valid entry and excluded invalid entries.
    assert(parsed.has.deviceAliases);
    assert(parsed.values.deviceAliases.size() == 1);
    assert(parsed.values.deviceAliases.at(L"dev:a") == L"Trimmed Name");

    // AlertThresholds kept valid entries and excluded out-of-range/wrong-type/tab keys.
    assert(parsed.has.alertThresholds);
    assert(parsed.values.alertThresholds.size() == 2);
    assert(parsed.values.alertThresholds.at(L"dev:a") == 20);
    assert(parsed.values.alertThresholds.at(L"dev:off") == -1);
}

void TestAlternativeValidFormsAndEscapes() {
    using namespace settings_port;

    // Legacy 4-item SystemOrder array, numeric uint32 SystemOrder, IgnoreDevices normalization,
    // Temperature alert match preset (1), empty Language, and Unicode surrogate pair escapes.
    const std::string json1 = R"({
        "SystemOrder": [3, 1],
        "IgnoreDevices": "0x046d:c547; 1532:84 046D:C547",
        "TempAlertCelsius": 1,
        "Language": "",
        "DeviceAliases": {
            "k:1": "Headset \uD83C\uDFA7 \u00DCber"
        },
        "HiddenDevices": ["dev:1", "", "dev:2"]
    })";
    ParsedSettings p1;
    assert(ParseSettingsJson(json1, p1));
    assert(p1.has.systemOrder);
    assert((p1.values.systemOrder == std::vector<int>{ 3, 1, 0, 2, 4, 5, 6 }));
    assert(p1.has.ignoreDevices && p1.values.ignoreDevices == L"046D:C547, 1532:0084");
    assert(p1.has.tempAlertCelsius && p1.values.tempAlertCelsius == 1);
    assert(p1.has.language && p1.values.language.empty());
    assert(p1.has.deviceAliases && p1.values.deviceAliases.at(L"k:1") == L"Headset \U0001F3A7 \u00DCber");
    assert(p1.has.hiddenDevices && p1.values.hiddenDevices.size() == 2);

    // Numeric uint32 SystemOrder (e.g. legacy 0xFFFF3210).
    ParsedSettings p2;
    assert(ParseSettingsJson(R"({"SystemOrder": 4294914576})", p2));
    assert(p2.has.systemOrder);

    ParsedSettings pBounds;
    assert(ParseSettingsJson(R"({"InGameAlerts":0,"InGameAlertCorner":2,"AlertSound":0,"EveningReminderHour":20,"PollLessFullscreen":true,"LocalApiPipe":false})", pBounds));
    assert(pBounds.acceptedCount == 6 && pBounds.rejectedFields == 0);
    assert(pBounds.values.inGameAlerts == 0 && pBounds.values.inGameAlertCorner == 2 &&
           pBounds.values.alertSound == 0 && pBounds.values.eveningReminderHour == 20 &&
           pBounds.values.pollLessFullscreen == true && pBounds.values.localApiPipe == false);
    assert((p2.values.systemOrder == std::vector<int>{ 0, 1, 2, 3, 4, 5, 6 }));

    // Bounds on HiddenDevices (256) and DeviceOrder (64) and >256 char entry filtered.
    std::string manyHidden = "{\"HiddenDevices\":[";
    for (int i = 0; i < 270; ++i) {
        if (i) manyHidden += ",";
        manyHidden += "\"k:" + std::to_string(i) + "\"";
    }
    manyHidden += ",\"" + std::string(257, 'a') + "\"]}";
    ParsedSettings p3;
    assert(ParseSettingsJson(manyHidden, p3));
    assert(p3.has.hiddenDevices && p3.values.hiddenDevices.size() == 256);

    std::string manyOrder = "{\"DeviceOrder\":[";
    for (int i = 0; i < 70; ++i) {
        if (i) manyOrder += ",";
        manyOrder += "\"k:" + std::to_string(i) + "\"";
    }
    manyOrder += ",\"" + std::string(257, 'b') + "\"]}";
    ParsedSettings p4;
    assert(ParseSettingsJson(manyOrder, p4));
    assert(p4.has.deviceOrder && p4.values.deviceOrder.size() == 64);
}

void TestMalformedAndBoundsRejected() {
    using namespace settings_port;

    ParsedSettings out;
    assert(!ParseSettingsJson("", out));
    assert(!ParseSettingsJson("[]", out));
    assert(!ParseSettingsJson("123", out));
    assert(!ParseSettingsJson("\"str\"", out));
    assert(!ParseSettingsJson("null", out));
    assert(!ParseSettingsJson("{", out));
    assert(!ParseSettingsJson("{\"BrandColors\": true,}", out));
    assert(!ParseSettingsJson("{\"BrandColors\": true} trailing", out));
    assert(!ParseSettingsJson("{\"BrandColors\": true, \"BrandColors\": false}", out));  // duplicate key
    assert(!ParseSettingsJson("{\"DeviceAliases\": {\"k\": \"\\uD800\"}}", out));         // lone surrogate
    assert(!ParseSettingsJson("{\"WidgetMonitor\": \"bad\nnewline\"}", out));            // unescaped control char
    assert(ParseSettingsJson("{\"PollIntervalSeconds\": 60.5}", out) && !out.has.pollIntervalSeconds);

    // Nesting depth > kMaxJsonDepth (8) rejected.
    std::string deep = "{\"a\":";
    for (int i = 0; i < 10; ++i) deep += "[";
    deep += "1";
    for (int i = 0; i < 10; ++i) deep += "]";
    deep += "}";
    assert(!ParseSettingsJson(deep, out));

    // Input > kMaxSettingsFileBytes rejected.
    std::string huge(kMaxSettingsFileBytes + 1, ' ');
    assert(!ParseSettingsJson(huge, out));
}

void TestUtcIso8601AndCsv() {
    using namespace settings_port;

    // Epoch, leap day 2024-02-29, standard timestamp, and negative pre-1970 timestamp.
    assert(FormatUtcIso8601(0) == "1970-01-01T00:00:00Z");
    assert(FormatUtcIso8601(1709210096LL) == "2024-02-29T12:34:56Z");
    assert(FormatUtcIso8601(1709251199LL) == "2024-02-29T23:59:59Z");
    assert(FormatUtcIso8601(1709251200LL) == "2024-03-01T00:00:00Z");
    assert(FormatUtcIso8601(951825600LL) == "2000-02-29T12:00:00Z");  // 2000 is leap year
    assert(FormatUtcIso8601(-1LL) == "1969-12-31T23:59:59Z");

    // RFC-4180 CSV escaping.
    assert(CsvEscape("plain") == "plain");
    assert(CsvEscape("a,b") == "\"a,b\"");
    assert(CsvEscape("say \"hi\"") == "\"say \"\"hi\"\"\"");
    assert(CsvEscape("line1\nline2") == "\"line1\nline2\"");
    assert(CsvEscape("line1\r\nline2") == "\"line1\r\nline2\"");
    assert(CsvEscape("Mysz \U0001F3A7") == "Mysz \U0001F3A7");

    // Empty history exports header only.
    assert(HistoryToCsv(L"dev:1", L"Mouse", {}) ==
           "device_key,device_name,utc_iso8601,percent,charging\n");

    // Populated history with special CSV characters and UTF-8 name.
    const std::vector<battery_history::Sample> samples = {
        { L"kysona:373B:1085", 1709210096LL, 85, false },
        { L"kysona:373B:1085", 1709213696LL, 92, true },
    };
    const std::string csv = HistoryToCsv(L"kysona:373B:1085", L"Mouse, \"Pro\" \u00e7", samples);
    const std::string expected =
        "device_key,device_name,utc_iso8601,percent,charging\n"
        "kysona:373B:1085,\"Mouse, \"\"Pro\"\" \xC3\xA7\",2024-02-29T12:34:56Z,85,false\n"
        "kysona:373B:1085,\"Mouse, \"\"Pro\"\" \xC3\xA7\",2024-02-29T13:34:56Z,92,true\n";
    assert(csv == expected);
}

}  // namespace

int main() {
    TestSettingsRoundTrip();
    TestFilteringAndValidation();
    TestAlternativeValidFormsAndEscapes();
    TestMalformedAndBoundsRejected();
    TestUtcIso8601AndCsv();
    std::puts("Settings port & CSV export tests passed");
    return 0;
}
