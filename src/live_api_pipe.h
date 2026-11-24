#pragma once
// Strictly read-only local named pipe server (\\.\pipe\TaskbarVitals) serving single JSON snapshots
// restricted to the current signed-in user SID.
#include <string>

namespace live_api_pipe {

inline constexpr wchar_t kDefaultPipeName[] = L"\\\\.\\pipe\\TaskbarVitals";

bool StartServer(const wchar_t* pipeName = kDefaultPipeName);
void Stop();
void UpdateSnapshot(std::string utf8Json);

}  // namespace live_api_pipe
