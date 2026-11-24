#include "event_decoders.h"

std::vector<std::unique_ptr<IHidEventDecoder>> HidEventDecoders() {
    std::vector<std::unique_ptr<IHidEventDecoder>> v;
    v.push_back(MakeLogitechEventDecoder());
    return v;
}
