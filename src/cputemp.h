#pragma once
// CPU temperature, shared by the app and its helper (helper/cputemp_helper.cpp).
//
// Reading a CPU's temperature needs a kernel driver and administrator rights, which the app itself does not have.
// So a small helper, TaskbarVitalsTemp.exe, does it: it runs elevated (started at sign-in by a scheduled task that
// the user approved once), reads two Intel MSRs through the PawnIO driver (https://pawnio.eu), and publishes the
// result in a shared memory block that the app only reads. The helper takes no commands from anyone and writes
// no MSR; the app can only ask it to stop.
//
// This header holds what both sides agree on, and the pure functions (temperature arithmetic, the task definition)
// so they can be tested without a driver.
#include <cstdint>
#include <string>

namespace cputemp {

// Named objects, in the session namespace. The helper creates them with a security descriptor that gives the
// signed-in user read access to the block and the right to signal the stop event, and nothing else.
constexpr wchar_t kMappingName[] = L"Local\\TaskbarVitalsCpuTemp";
constexpr wchar_t kStopEventName[] = L"Local\\TaskbarVitalsCpuTempStop";
constexpr wchar_t kHelperMutexName[] = L"Global\\TaskbarVitalsCpuTempHelper";

constexpr wchar_t kTaskName[] = L"Taskbar Vitals CPU temperature";
constexpr wchar_t kInstallFolder[] = L"Taskbar Vitals";       // under Program Files: writable by administrators only
constexpr wchar_t kHelperFile[] = L"TaskbarVitalsTemp.exe";
constexpr wchar_t kSettingValue[] = L"ShowCpuTemp";            // HKCU\Software\WirelessBattery: the helper quits when it is off
constexpr wchar_t kMainWindowClass[] = L"WirelessBatteryWnd";  // the helper quits when the app is gone

constexpr std::uint32_t kMagic = 0x50545654;  // "TVTP"
constexpr std::uint32_t kVersion = 1;

enum class Status : std::uint32_t {
    Starting = 0,
    Ok = 1,
    NoPawnIo = 2,       // the PawnIO driver is not installed
    ModuleFailed = 3,   // the driver refused or could not load the module
    Unsupported = 4,    // not an Intel x64 CPU, or it reports no package thermal reading
    ReadFailed = 5,
};

struct Shared {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint32_t status = 0;      // Status
    std::int32_t tempC = -1;       // CPU package temperature, valid when status is Ok
    std::int32_t tjMax = 0;
    std::uint32_t helperVersion = 0;
    std::uint64_t tick = 0;        // GetTickCount64 at the last update
};

// MSR numbers the helper reads (and nothing else) on Intel CPUs.
constexpr std::uint32_t kMsrPackageThermStatus = 0x1B1;  // IA32_PACKAGE_THERM_STATUS
constexpr std::uint32_t kMsrTemperatureTarget = 0x1A2;   // IA32_TEMPERATURE_TARGET (MSR_TEMPERATURE_TARGET)

// AMD Family 17h-1Ah SMN thermal register (THM_TCON_CUR_TMP).
constexpr std::uint32_t kAmdSmnThmTconCurTmp = 0x00059800;

// TjMax, the temperature at which the CPU throttles: bits 23:16 of the temperature target. A value outside
// 60..125 is not believable, and 100 is what nearly every Intel CPU uses.
inline int TjMax(std::uint64_t temperatureTarget) {
    int t = (int)((temperatureTarget >> 16) & 0xFF);
    return t < 60 || t > 125 ? 100 : t;
}

// Package temperature in degrees Celsius from IA32_PACKAGE_THERM_STATUS: bit 31 says the reading is valid, bits
// 22:16 are the degrees below TjMax. -1 when the reading is not valid or not believable.
inline int PackageTemperature(std::uint64_t thermStatus, int tjMax) {
    if (!((thermStatus >> 31) & 1)) return -1;
    int below = (int)((thermStatus >> 16) & 0x7F);
    int t = tjMax - below;
    return t < 0 || t > 125 ? -1 : t;
}

// Known Tctl -> Tdie offsets (Celsius) for first-generation Ryzen and Threadripper X SKUs.
inline int AmdTctlOffset(const std::string& brand) {
    if (brand.find("Threadripper 19") != std::string::npos ||
        brand.find("Threadripper 29") != std::string::npos) return -27;
    if (brand.find("1600X") != std::string::npos ||
        brand.find("1700X") != std::string::npos ||
        brand.find("1800X") != std::string::npos) return -20;
    if (brand.find("2600X") != std::string::npos ||
        brand.find("2700X") != std::string::npos) return -10;
    return 0;
}

// Converts THM_TCON_CUR_TMP (SMN 0x00059800) to Tdie in degrees Celsius: bits 31:21 in 0.125 C steps,
// minus 49 C when bit 19 (CUR_TEMP_RANGE_SEL) is set, plus SKU Tctl offset. -1 when out of range.
inline int AmdPackageTemperature(std::uint32_t regVal, int tctlOffset = 0) {
    double t = ((regVal >> 21) & 0x7FF) * 0.125;
    if ((regVal >> 19) & 1U) t -= 49.0;
    t += tctlOffset;
    int rounded = (int)(t >= 0.0 ? t + 0.5 : t - 0.5);
    return rounded < 0 || rounded > 125 ? -1 : rounded;
}

// Whether a block read by the app is one the helper wrote, and not too old to show.
inline bool Valid(const Shared& s) { return s.magic == kMagic && s.version == kVersion; }
inline bool Fresh(const Shared& s, std::uint64_t nowTick, std::uint64_t maxAgeMs = 10000) {
    return Valid(s) && nowTick >= s.tick && nowTick - s.tick <= maxAgeMs;
}

inline std::wstring XmlEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// The scheduled task that starts the helper at sign-in, with the highest privileges, for one user (a SID string).
// It runs `exePath` only; exePath must be in a folder that only administrators can write to.
inline std::wstring TaskXml(const std::wstring& userSid, const std::wstring& exePath) {
    const std::wstring sid = XmlEscape(userSid), exe = XmlEscape(exePath);
    return L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
           L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
           L"  <RegistrationInfo>\r\n"
           L"    <Description>Reads the CPU temperature for Taskbar Vitals (needs administrator rights). "
           L"Remove it from the Taskbar Vitals settings.</Description>\r\n"
           L"  </RegistrationInfo>\r\n"
           L"  <Triggers>\r\n"
           L"    <LogonTrigger><Enabled>true</Enabled><UserId>" + sid + L"</UserId></LogonTrigger>\r\n"
           L"  </Triggers>\r\n"
           L"  <Principals>\r\n"
           L"    <Principal id=\"Author\">\r\n"
           L"      <UserId>" + sid + L"</UserId>\r\n"
           L"      <LogonType>InteractiveToken</LogonType>\r\n"
           L"      <RunLevel>HighestAvailable</RunLevel>\r\n"
           L"    </Principal>\r\n"
           L"  </Principals>\r\n"
           L"  <Settings>\r\n"
           L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
           L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
           L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
           L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
           L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
           L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
           L"    <Enabled>true</Enabled>\r\n"
           L"    <Hidden>false</Hidden>\r\n"
           L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
           L"    <Priority>7</Priority>\r\n"
           L"  </Settings>\r\n"
           L"  <Actions Context=\"Author\">\r\n"
           L"    <Exec><Command>" + exe + L"</Command></Exec>\r\n"
           L"  </Actions>\r\n"
           L"</Task>\r\n";
}

}  // namespace cputemp
