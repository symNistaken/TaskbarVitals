// CPU temperature arithmetic, the shared block checks and the scheduled task definition.
#include <cassert>
#include <cstdio>

#include "../src/cputemp.h"

using namespace cputemp;

int main() {
    // TjMax from bits 23:16 of the temperature target; an unbelievable value falls back to 100.
    assert(TjMax(0x00640000) == 100 && TjMax(0x00550000) == 85 && TjMax(0x006E0000) == 110);
    assert(TjMax(0) == 100 && TjMax(0x00100000) == 100 && TjMax(0x00FF0000) == 100);
    assert(TjMax(0xFFFF00640000FFFFULL) == 100);  // other bits do not matter

    // Package temperature: valid bit 31, degrees below TjMax in bits 22:16.
    const std::uint64_t valid = 1ULL << 31;
    assert(PackageTemperature(valid | (40ULL << 16), 100) == 60);
    assert(PackageTemperature(valid | (0ULL << 16), 100) == 100);
    assert(PackageTemperature(valid | (15ULL << 16), 85) == 70);
    assert(PackageTemperature((40ULL << 16), 100) == -1);                 // reading not valid
    assert(PackageTemperature(valid | (120ULL << 16), 100) == -1);        // below zero: not believable
    assert(PackageTemperature(valid | (0x7FULL << 16), 125) == -1);       // 127 below 125 would be -2
    assert(PackageTemperature(valid | (40ULL << 16) | 0xFFFF, 100) == 60);  // low bits are ignored

    // The block the app reads: wrong magic or version is not ours; an old one is not shown.
    Shared s;
    assert(!Valid(s) && !Fresh(s, 0));
    s.magic = kMagic;
    s.version = kVersion;
    s.tick = 1000;
    assert(Valid(s) && Fresh(s, 1000) && Fresh(s, 10000) && !Fresh(s, 11001));
    assert(!Fresh(s, 999));  // a tick from the future is not fresh
    s.version = kVersion + 1;
    assert(!Valid(s));

    // The task: this user, the highest privileges, at sign-in, the one program, and escaped text.
    std::wstring xml = TaskXml(L"S-1-5-21-1-2-3-1001", L"C:\\Program Files\\Taskbar Vitals\\TaskbarVitalsTemp.exe");
    assert(xml.find(L"<RunLevel>HighestAvailable</RunLevel>") != std::wstring::npos);
    assert(xml.find(L"<LogonTrigger>") != std::wstring::npos);
    assert(xml.find(L"<UserId>S-1-5-21-1-2-3-1001</UserId>") != std::wstring::npos);
    assert(xml.find(L"<Command>C:\\Program Files\\Taskbar Vitals\\TaskbarVitalsTemp.exe</Command>") != std::wstring::npos);
    assert(xml.find(L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>") != std::wstring::npos);
    assert(xml.find(L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>") != std::wstring::npos);
    // exactly one Exec action
    size_t first = xml.find(L"<Exec>");
    assert(first != std::wstring::npos && xml.find(L"<Exec>", first + 1) == std::wstring::npos);
    // special characters in a path cannot break out of the element
    std::wstring tricky = TaskXml(L"S-1", L"C:\\A&B\\<x>\"y\".exe");
    assert(tricky.find(L"C:\\A&amp;B\\&lt;x&gt;&quot;y&quot;.exe") != std::wstring::npos);
    assert(tricky.find(L"<x>") == std::wstring::npos);

    std::puts("CPU temperature tests passed (TjMax, package reading, shared block age, task definition, escaping).");
    return 0;
}
