// Protocol reference: https://github.com/OpenMouse-Project/mouse-protocol/blob/main/docs/redragon-m690-pro-testing.md
#include "drivers.h"

namespace {
bool Query(HidDev& device, BYTE command, std::vector<BYTE>& reply) {
    std::vector<BYTE> request(8, 0);
    request[0] = 5; request[1] = command;
    if (!device.SetFeature(request)) return false;
    Sleep(100);
    return device.GetFeature(5, reply) && reply.size() == 8 &&
           reply[0] == 5 && reply[1] == command;
}
}

void PollSinowealthReceiver(PollContext& ctx) {
    if (ctx.IsClaimed(0x258A, 0x002F)) return;
    // Short status reports documented by OpenMouse for M690 Pro and verified
    // on an M602-KS. The USB ID alone is shared by other receivers.
    for (const auto* c : ctx.Find(0x258A, 0x002F, [](const HidColl& c) {
             return c.iface == 1 && c.page() == 0xFF00 && c.usage() == 1 &&
                    c.featLen() == 8 && c.inLen() == 0 && c.outLen() == 0;
         })) {
        HidDev device;
        if (!device.OpenFeatureOnly(*c)) continue;
        std::vector<BYTE> link, battery;
        bool valid = Query(device, 0x80, link) && link[2] == 1 &&
                     Query(device, 0x90, battery) && battery[2] == 0x11 && battery[3] <= 100;
        if (!valid) continue;

        DeviceStatus s;
        s.key = L"sinowealth:258A:002F";
        s.name = L"Sinowealth wireless mouse";
        s.via = L"2.4 GHz receiver";
        s.kind = DeviceKind::Mouse;
        s.percent = battery[3];
        ctx.Add(s, c->vid, c->pid);
        break;
    }
}
