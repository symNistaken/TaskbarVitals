#pragma once
// The CPU / RAM / GPU strip: a second copy of the taskbar strip with its own window, items and position, so the
// readings can sit apart from the batteries (for example the batteries at the left end of the taskbar and the
// readings next to the clock). Same API as namespace widget; built from widget.cpp by vitals_strip.cpp.
#include "widget.h"

namespace vitals_strip {
#include "widget_api.h"
}
