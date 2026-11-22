#pragma once
// One BLE advertisement watcher (WinRT BluetoothLEAdvertisementWatcher, passive scan) shared by the
// drivers that read battery levels from broadcasts. It only listens; nothing is ever sent.
// Received messages are handed to the parsers below on a WinRT thread pool thread, so they must
// lock whatever they store.
#include <windows.h>

// Starts the watcher, or restarts it after the radio was switched off. Returns true when it was just
// started, so the caller can give devices a moment (they broadcast about every 1-2 s) to be heard.
bool EnsureBleWatcher();

// Apple manufacturer data (company 0x004C), see airpods.cpp.
void AirPodsOnAdvert(UINT64 addr, int rssi, const BYTE* data, UINT32 len);
// Google Fast Pair service data (16-bit UUID 0xFE2C, bytes after the UUID), see fastpair.cpp.
void FastPairOnAdvert(UINT64 addr, const BYTE* data, UINT32 len);

// Battery split from the latest Fast Pair broadcast (percent, -1 unknown).
struct FastPairBattery {
    int left = -1, right = -1, box = -1;
    bool leftChg = false, rightChg = false, boxChg = false;
    ULONGLONG tick = 0;
};
// Latest broadcast of the device with this Bluetooth address, if heard in the last 5 minutes.
bool FastPairLookup(UINT64 addr, FastPairBattery& out);
