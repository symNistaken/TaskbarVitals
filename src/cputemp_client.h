#pragma once
// The app's side of the CPU temperature (see cputemp.h): reads what the helper publishes, and sets the helper up,
// starts it and stops it. The app itself never touches the driver and never runs elevated.
#include <windows.h>

#include "cputemp.h"

namespace cputemp {

struct Reading {
    int tempC = -1;                   // CPU package temperature; -1 when there is none to show
    Status status = Status::Starting;
    bool running = false;             // a fresh block from the helper was found
};

// Reads the helper's shared block. Opens and closes it each time on purpose: a handle held here would keep the name
// alive and stop a restarted helper from creating it.
Reading ReadShared();

// Whether the PawnIO driver is installed (its uninstall entry, or %ProgramFiles%\PawnIO).
bool PawnIoInstalled();
// Whether the helper is installed in Program Files and so can be started by its task.
bool HelperInstalled();

enum class InstallResult { Ok, Cancelled, Failed };
// Runs `TaskbarVitalsTemp.exe --install` from the app's folder with a Windows permission prompt (once).
InstallResult InstallHelper(HWND owner);
// Runs `TaskbarVitalsTemp.exe --remove` with a Windows permission prompt: deletes the task and the copy.
bool RemoveHelper(HWND owner);

// Starts the sign-in task now (no prompt; nothing happens if the helper already runs) / asks the helper to quit.
void StartHelper();
void StopHelper();

}  // namespace cputemp
