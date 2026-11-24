#pragma once
// Razor-sharp 4x4 supersampled vector icon renderer for Taskbar Vitals devices and sensors.
// Avoids blurry font-glyph scaling on the taskbar and works identically across Windows 10/11 DPIs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "devices.h"

namespace vector_icons {

enum class VectorIcon {
    None = 0,
    Headset,
    Mouse,
    Keyboard,
    Gamepad,
    Phone,
    Microphone,
    Speaker,
    Bluetooth,
    GenericDevice,
    Cpu,
    Ram,
    Gpu,
    Vram,
    Disk,
    Network,
    LaptopBattery,
    BatteryUnknown,
};

struct IconMask {
    int w = 0, h = 0;
    std::vector<unsigned char> a;
};

inline VectorIcon ForDevice(DeviceKind k, bool bluetooth) {
    switch (k) {
        case DeviceKind::Headset:    return VectorIcon::Headset;
        case DeviceKind::Mouse:      return VectorIcon::Mouse;
        case DeviceKind::Keyboard:   return VectorIcon::Keyboard;
        case DeviceKind::Gamepad:    return VectorIcon::Gamepad;
        case DeviceKind::Phone:      return VectorIcon::Phone;
        case DeviceKind::Microphone: return VectorIcon::Microphone;
        case DeviceKind::Speaker:    return VectorIcon::Speaker;
        default:                     return bluetooth ? VectorIcon::Bluetooth : VectorIcon::GenericDevice;
    }
}

namespace detail {

inline float DistSeg(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 1e-6f ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

inline float DistRoundBoxFill(float px, float py, float cx, float cy, float hx, float hy, float r) {
    float qx = std::fabs(px - cx) - std::max(0.0f, hx - r);
    float qy = std::fabs(py - cy) - std::max(0.0f, hy - r);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}

inline float DistRoundBoxStroke(float px, float py, float cx, float cy, float hx, float hy, float r, float halfW) {
    return std::fabs(DistRoundBoxFill(px, py, cx, cy, hx, hy, r)) - halfW;
}

inline float DistCircleStroke(float px, float py, float cx, float cy, float r, float halfW) {
    return std::fabs(std::hypot(px - cx, py - cy) - r) - halfW;
}

inline float DistCircleFill(float px, float py, float cx, float cy, float r) {
    return std::hypot(px - cx, py - cy) - r;
}

// Signed distance in normalized [0..24]x[0..24] design space; <= 0 is filled.
inline float Sdf(VectorIcon icon, float x, float y) {
    float d = 1e6f;
    auto strokeSeg = [&](float ax, float ay, float bx, float by, float w = 1.85f) {
        d = std::min(d, DistSeg(x, y, ax, ay, bx, by) - w * 0.5f);
    };
    auto strokeBox = [&](float l, float t, float r, float b, float rad, float w = 1.8f) {
        d = std::min(d, DistRoundBoxStroke(x, y, (l + r) * 0.5f, (t + b) * 0.5f, (r - l) * 0.5f, (b - t) * 0.5f, rad, w * 0.5f));
    };
    auto fillBox = [&](float l, float t, float r, float b, float rad) {
        d = std::min(d, DistRoundBoxFill(x, y, (l + r) * 0.5f, (t + b) * 0.5f, (r - l) * 0.5f, (b - t) * 0.5f, rad));
    };
    auto dot = [&](float cx, float cy, float rad) {
        d = std::min(d, DistCircleFill(x, y, cx, cy, rad));
    };

    switch (icon) {
        case VectorIcon::Cpu: {
            strokeBox(6.2f, 6.2f, 17.8f, 17.8f, 2.2f, 1.8f);
            fillBox(9.6f, 9.6f, 14.4f, 14.4f, 0.9f);
            for (float p : { 9.2f, 12.0f, 14.8f }) {
                strokeSeg(p, 2.6f, p, 6.2f, 1.6f);
                strokeSeg(p, 17.8f, p, 21.4f, 1.6f);
                strokeSeg(2.6f, p, 6.2f, p, 1.6f);
                strokeSeg(17.8f, p, 21.4f, p, 1.6f);
            }
            break;
        }
        case VectorIcon::Ram:
        case VectorIcon::Vram: {
            strokeBox(2.4f, 6.2f, 21.6f, 15.6f, 1.6f, 1.75f);
            for (float cx : { 6.2f, 10.1f, 13.9f, 17.8f })
                fillBox(cx - 1.1f, 8.9f, cx + 1.1f, 12.9f, 0.4f);
            // Gold-finger connector teeth along bottom edge with notch in center.
            for (float p : { 4.8f, 7.6f, 10.2f, 13.8f, 16.4f, 19.2f })
                strokeSeg(p, 15.6f, p, 18.8f, 1.55f);
            if (icon == VectorIcon::Vram) {
                strokeSeg(5.0f, 3.3f, 19.0f, 3.3f, 1.4f);
            }
            break;
        }
        case VectorIcon::Gpu: {
            // Graphics card shroud with dual cooling fans & PCIe edge connector.
            strokeBox(2.4f, 5.4f, 21.6f, 16.6f, 2.0f, 1.8f);
            d = std::min(d, DistCircleStroke(x, y, 8.2f, 11.0f, 2.9f, 0.78f));
            d = std::min(d, DistCircleStroke(x, y, 15.8f, 11.0f, 2.9f, 0.78f));
            dot(8.2f, 11.0f, 0.9f);
            dot(15.8f, 11.0f, 0.9f);
            strokeSeg(5.5f, 16.6f, 5.5f, 19.3f, 1.7f);
            strokeSeg(8.5f, 19.3f, 16.5f, 19.3f, 1.7f);
            strokeSeg(10.5f, 16.6f, 10.5f, 19.3f, 1.7f);
            break;
        }
        case VectorIcon::Disk: {
            // NVMe M.2 / SSD module: crisp chassis + activity LED + platter/slot bar.
            strokeBox(4.0f, 3.0f, 20.0f, 21.0f, 2.4f, 1.85f);
            strokeSeg(7.2f, 16.6f, 16.8f, 16.6f, 1.75f);
            dot(16.2f, 7.4f, 1.15f);
            strokeSeg(7.2f, 7.4f, 12.2f, 7.4f, 1.65f);
            break;
        }
        case VectorIcon::Network: {
            // Sharp dual opposing arrows (download down + upload up).
            strokeSeg(7.5f, 4.5f, 7.5f, 18.5f, 2.0f);
            strokeSeg(4.0f, 14.5f, 7.5f, 18.8f, 2.0f);
            strokeSeg(11.0f, 14.5f, 7.5f, 18.8f, 2.0f);
            strokeSeg(16.5f, 5.5f, 16.5f, 19.5f, 2.0f);
            strokeSeg(13.0f, 9.5f, 16.5f, 5.2f, 2.0f);
            strokeSeg(20.0f, 9.5f, 16.5f, 5.2f, 2.0f);
            break;
        }
        case VectorIcon::Mouse: {
            strokeBox(6.2f, 2.6f, 17.8f, 21.4f, 5.8f, 1.85f);
            strokeSeg(12.0f, 2.8f, 12.0f, 8.8f, 1.6f);
            fillBox(11.1f, 5.5f, 12.9f, 9.2f, 0.9f);
            break;
        }
        case VectorIcon::Headset: {
            // Top headband arc (upper semicircle) + twin earcups.
            float arcD = DistCircleStroke(x, y, 12.0f, 12.6f, 8.0f, 0.95f);
            if (y <= 13.5f) d = std::min(d, arcD);
            strokeSeg(4.0f, 12.5f, 4.0f, 16.5f, 1.85f);
            strokeSeg(20.0f, 12.5f, 20.0f, 16.5f, 1.85f);
            fillBox(2.7f, 13.0f, 6.6f, 20.2f, 1.7f);
            fillBox(17.4f, 13.0f, 21.3f, 20.2f, 1.7f);
            break;
        }
        case VectorIcon::Keyboard: {
            strokeBox(2.2f, 5.5f, 21.8f, 18.5f, 2.2f, 1.8f);
            for (float px : { 5.8f, 8.9f, 12.0f, 15.1f, 18.2f }) dot(px, 9.2f, 0.95f);
            for (float px : { 6.8f, 9.9f, 13.1f, 16.5f }) dot(px, 12.1f, 0.95f);
            strokeSeg(7.2f, 15.1f, 16.8f, 15.1f, 1.7f);
            break;
        }
        case VectorIcon::Gamepad: {
            strokeBox(2.8f, 6.2f, 21.2f, 16.8f, 4.4f, 1.85f);
            // Grips
            strokeSeg(5.0f, 15.2f, 3.8f, 19.2f, 2.1f);
            strokeSeg(19.0f, 15.2f, 20.2f, 19.2f, 2.1f);
            // D-pad & AB buttons
            strokeSeg(7.6f, 9.4f, 7.6f, 13.6f, 1.6f);
            strokeSeg(5.5f, 11.5f, 9.7f, 11.5f, 1.6f);
            dot(15.2f, 10.2f, 1.0f);
            dot(17.6f, 12.6f, 1.0f);
            break;
        }
        case VectorIcon::Phone: {
            strokeBox(6.6f, 2.4f, 17.4f, 21.6f, 2.2f, 1.85f);
            strokeSeg(10.0f, 18.6f, 14.0f, 18.6f, 1.55f);
            break;
        }
        case VectorIcon::Microphone: {
            strokeBox(8.6f, 2.8f, 15.4f, 13.6f, 3.4f, 1.8f);
            float cradle = DistCircleStroke(x, y, 12.0f, 10.6f, 5.8f, 0.9f);
            if (y >= 10.5f) d = std::min(d, cradle);
            strokeSeg(12.0f, 16.4f, 12.0f, 20.6f, 1.8f);
            strokeSeg(8.4f, 20.6f, 15.6f, 20.6f, 1.8f);
            break;
        }
        case VectorIcon::Speaker: {
            strokeBox(5.2f, 2.6f, 18.8f, 21.4f, 2.4f, 1.85f);
            dot(12.0f, 7.2f, 1.35f);
            d = std::min(d, DistCircleStroke(x, y, 12.0f, 15.2f, 3.4f, 0.9f));
            dot(12.0f, 15.2f, 1.15f);
            break;
        }
        case VectorIcon::Bluetooth: {
            strokeSeg(12.0f, 3.0f, 12.0f, 21.0f, 1.9f);
            strokeSeg(12.0f, 3.0f, 17.5f, 7.6f, 1.85f);
            strokeSeg(17.5f, 7.6f, 6.5f, 16.4f, 1.85f);
            strokeSeg(12.0f, 21.0f, 17.5f, 16.4f, 1.85f);
            strokeSeg(17.5f, 16.4f, 6.5f, 7.6f, 1.85f);
            break;
        }
        case VectorIcon::BatteryUnknown: {
            strokeBox(2.6f, 6.6f, 19.2f, 17.4f, 2.6f, 1.85f);
            fillBox(19.2f, 9.6f, 21.4f, 14.4f, 0.9f);
            strokeSeg(8.2f, 12.0f, 13.6f, 12.0f, 1.8f);
            break;
        }
        case VectorIcon::LaptopBattery: {
            strokeBox(2.4f, 6.0f, 19.2f, 18.0f, 2.4f, 1.8f);
            fillBox(19.2f, 9.4f, 21.6f, 14.6f, 0.9f);
            fillBox(5.2f, 8.9f, 8.4f, 15.1f, 0.6f);
            fillBox(10.2f, 8.9f, 13.4f, 15.1f, 0.6f);
            break;
        }
        case VectorIcon::GenericDevice:
        default: {
            strokeBox(5.0f, 4.4f, 19.0f, 19.6f, 3.0f, 1.85f);
            dot(12.0f, 12.0f, 2.2f);
            break;
        }
    }
    return d;
}

}  // namespace detail

// Renders `icon` into a tight-cropped grayscale coverage mask sized to `sizePx` pixels tall/wide.
inline IconMask Render(VectorIcon icon, int sizePx) {
    IconMask out;
    if (icon == VectorIcon::None || sizePx <= 0) return out;
    const int s = std::clamp(sizePx, 8, 64);
    out.w = s;
    out.h = s;
    out.a.assign((size_t)s * s, 0);
    const float invScale = 24.0f / (float)s;
    const float feather = 1.0f * invScale;  // 1 pixel AA band in design space

    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            float sum = 0.0f;
            for (int sy = 0; sy < 4; ++sy) {
                float py = (y + (sy + 0.5f) * 0.25f) * invScale;
                for (int sx = 0; sx < 4; ++sx) {
                    float px = (x + (sx + 0.5f) * 0.25f) * invScale;
                    float d = detail::Sdf(icon, px, py);
                    float cov = std::clamp(0.5f - d / std::max(0.15f, feather * 0.65f), 0.0f, 1.0f);
                    sum += cov;
                }
            }
            out.a[(size_t)y * s + x] = (unsigned char)std::clamp((int)(sum * (255.0f / 16.0f) + 0.5f), 0, 255);
        }
    }
    return out;
}

}  // namespace vector_icons
