#pragma once
#include <windows.h>
#include <set>
#include <string>

namespace battery_history {
// Called once before polling starts. Demo data is ephemeral and never written to disk.
void Initialize(bool demo);
// Keys that had readings in the history file when Initialize() ran. Filled once before polling starts and
// never changed afterwards, so the poll thread may read it.
const std::set<std::wstring>& KeysAtStartup();
// Stores only valid, connected and awake readings (the caller enforces connection state).
void Observe(const std::wstring& key, int percent, bool charging);
// Non-modal native Win32 history viewer; device identity remains the original key.
void Show(HWND owner, const std::wstring& key, const std::wstring& displayName);
} // namespace battery_history
