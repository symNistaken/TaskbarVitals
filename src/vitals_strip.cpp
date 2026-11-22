// The second copy of the taskbar strip (see vitals_strip.h): widget.cpp built again as namespace vitals_strip,
// with its own window class so both copies can be registered side by side.
#include "vitals_strip.h"

#define WIDGET_NS vitals_strip
#define WIDGET_CLASS L"WirelessBatteryVitals"
#include "widget.cpp"
