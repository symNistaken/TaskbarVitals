#include "status_json.h"

#include <windows.h>

#include <cstdio>

namespace {

std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::string Quoted(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size() + 2);
    out.push_back('"');
    for (unsigned char c : utf8) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);  // UTF-8 bytes pass through as they are
                }
        }
    }
    out.push_back('"');
    return out;
}

const char* Bool(bool v) { return v ? "true" : "false"; }

// Locale-independent 1-decimal number so Turkish/European locales never emit a comma separator.
std::string Fixed1(double v) {
    if (v != v) return "0.0";
    bool neg = v < 0.0;
    long long tenths = (long long)( (neg ? -v : v) * 10.0 + 0.5 );
    if (tenths == 0) neg = false;
    return (neg ? "-" : "") + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

std::string Whole(double v) {
    if (v < 0.0 || v != v) return "-1";
    return std::to_string((long long)(v + 0.5));
}

std::string VitalsJsonBlock(const StatusVitals& v) {
    std::string out;
    out += "  \"vitals\": {\n";
    out += "    \"cpu\": {\"percent\": " + std::to_string(v.cpuPercent) +
           ", \"tempC\": " + std::to_string(v.cpuTempC) +
           ", \"watts\": " + std::to_string(v.cpuWatts) + "},\n";
    out += "    \"ram\": {\"percent\": " + std::to_string(v.ramPercent) +
           ", \"usedGb\": " + Fixed1(v.ramUsedGb) +
           ", \"totalGb\": " + Fixed1(v.ramTotalGb) + "},\n";
    out += "    \"gpu\": {\"available\": " + std::string(Bool(v.gpuAvailable)) +
           ", \"name\": " + JsonQuote(v.gpuName) +
           ", \"percent\": " + std::to_string(v.gpuPercent) +
           ", \"tempC\": " + std::to_string(v.gpuTempC) +
           ", \"watts\": " + std::to_string(v.gpuWatts) +
           ", \"gpuClockMhz\": " + std::to_string(v.gpuClockMhz) +
           ", \"vramClockMhz\": " + std::to_string(v.vramClockMhz) +
           ", \"vramPercent\": " + std::to_string(v.vramPercent) +
           ", \"vramUsedGb\": " + Fixed1(v.vramUsedGb) +
           ", \"vramTotalGb\": " + Fixed1(v.vramTotalGb) + "},\n";
    out += "    \"storage\": {\"available\": " + std::string(Bool(v.diskAvailable)) +
           ", \"name\": " + JsonQuote(v.diskName) +
           ", \"drive\": " + JsonQuote(v.diskDrive) +
           ", \"tempC\": " + std::to_string(v.diskTempC) +
           ", \"percent\": " + std::to_string(v.diskPercent) +
           ", \"readMBs\": " + Fixed1(v.diskReadMBs) +
           ", \"writeMBs\": " + Fixed1(v.diskWriteMBs) +
           ", \"freeGb\": " + Fixed1(v.diskFreeGb) +
           ", \"totalGb\": " + Fixed1(v.diskTotalGb) + "},\n";
    out += "    \"network\": {\"available\": " + std::string(Bool(v.netAvailable)) +
           ", \"downBytesPerSec\": " + Whole(v.netDownBytesPerSec) +
           ", \"upBytesPerSec\": " + Whole(v.netUpBytesPerSec) +
           ", \"speedTestDownMbps\": " + (v.netSpeedTestDownMbps >= 0.0 ? Fixed1(v.netSpeedTestDownMbps) : "-1") +
           ", \"speedTestUpMbps\": " + (v.netSpeedTestUpMbps >= 0.0 ? Fixed1(v.netSpeedTestUpMbps) : "-1") + "},\n";
    out += "    \"laptopBattery\": {\"present\": " + std::string(Bool(v.laptopBatteryPresent)) +
           ", \"percent\": " + std::to_string(v.laptopBatteryPercent) +
           ", \"charging\": " + Bool(v.laptopBatteryCharging) +
           ", \"acOnline\": " + Bool(v.laptopBatteryAcOnline) +
           ", \"watts\": " + (v.laptopBatteryHasWatts ? Fixed1(v.laptopBatteryWatts) : "null") +
           ", \"remainingMinutes\": " + std::to_string(v.laptopBatteryRemainingMinutes) + "}\n";
    out += "  }";
    return out;
}

}  // namespace

const char* KindId(DeviceKind kind) {
    switch (kind) {
        case DeviceKind::Headset:  return "headset";
        case DeviceKind::Mouse:    return "mouse";
        case DeviceKind::Keyboard: return "keyboard";
        case DeviceKind::Gamepad:  return "gamepad";
        case DeviceKind::Phone:    return "phone";
        default:                   return "other";
    }
}

std::string JsonQuote(const std::wstring& text) { return Quoted(Utf8(text)); }

std::string BuildStatusJson(const std::vector<StatusDevice>& devices, const std::string& source,
                            int lowThreshold, const std::string& updated,
                            const StatusVitals* vitals) {
    std::string out;
    out += "{\n";
    out += "  \"version\": 1,\n";
    out += "  \"updated\": " + Quoted(updated) + ",\n";
    out += "  \"source\": " + Quoted(source) + ",\n";
    out += "  \"devices\": [";
    for (size_t i = 0; i < devices.size(); i++) {
        const StatusDevice& d = devices[i];
        int shown = d.percent >= 0 ? d.percent : d.lastPct;  // what the widget shows: live or last known
        out += i ? ",\n    {" : "\n    {";
        out += "\"key\": " + JsonQuote(d.key);
        out += ", \"name\": " + JsonQuote(d.name);
        out += ", \"kind\": \"" + std::string(KindId(d.kind)) + "\"";
        out += ", \"via\": " + JsonQuote(d.via);
        out += ", \"percent\": " + std::to_string(shown);
        out += std::string(", \"live\": ") + Bool(d.percent >= 0);
        out += std::string(", \"charging\": ") + Bool(d.charging);
        out += std::string(", \"asleep\": ") + Bool(d.asleep);
        out += std::string(", \"connected\": ") + Bool(d.connected);
        out += std::string(", \"low\": ") + Bool(!d.charging && shown >= 0 && shown <= lowThreshold);
        out += ", \"levelText\": " + JsonQuote(d.levelText);
        out += ", \"detail\": " + JsonQuote(d.detail);
        out += ", \"millivolts\": " + std::to_string(d.millivolts);
        out += "}";
    }
    if (!devices.empty()) out += "\n  ";
    out += "]";
    if (vitals && vitals->included) {
        out += ",\n";
        out += VitalsJsonBlock(*vitals);
    }
    out += "\n}\n";
    return out;
}

std::string IsoTimestampNow() {
    SYSTEMTIME t;
    GetSystemTime(&t);  // UTC
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
    return buf;
}

std::wstring StatusSnapshotPath() {
    wchar_t dir[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) {
        wchar_t tmp[MAX_PATH] = {};
        DWORD m = GetTempPathW(MAX_PATH, tmp);
        if (!m || m >= MAX_PATH) return L"WirelessBattery-status.json";  // no usable folder: current directory
        return std::wstring(tmp) + L"WirelessBattery\\status.json";
    }
    return std::wstring(dir) + L"\\WirelessBattery\\status.json";
}

bool WriteStatusSnapshot(const std::vector<StatusDevice>& devices, const std::string& source, int lowThreshold,
                         const StatusVitals* vitals) {
    std::wstring path = StatusSnapshotPath();
    size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos || slash == 0) return false;
    CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);  // ERROR_ALREADY_EXISTS is fine

    std::string json = BuildStatusJson(devices, source, lowThreshold, IsoTimestampNow(), vitals);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(f, json.data(), (DWORD)json.size(), &written, nullptr) != 0 && written == json.size();
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}
