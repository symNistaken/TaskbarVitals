
#include "../src/drivers/phonelink_protocol.h"

#include <cassert>
#include <cstdio>

int main() {
    PhoneLink::Reading reading;

    // Existing English card: battery and charging.
    const std::wstring english = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "body": [
            {"tooltip": "Open Phone Link"},
            {"text": "73%", "tooltip": "Battery 73% remaining, charging"}
        ]
    })";

    assert(PhoneLink::Parse(english, reading));
    assert(reading.name == L"Pixel 8");
    assert(reading.percent == 73);
    assert(reading.charging);

    // Japanese card from issue #2738.
    const std::wstring japanese = LR"({
        "speak": "iPhone - PC に接続しました",
        "body": [
            {
                "tooltip": "バッテリー 残量 55%",
                "altText": "現在、バッテリー 残量は  55%です",
                "text": "55%"
            }
        ]
    })";

    reading = {};
    assert(PhoneLink::Parse(japanese, reading));
    assert(reading.name == L"iPhone");
    assert(reading.percent == 55);
    assert(!reading.charging);

    // Reject disconnected/setup cards, even with a battery-like tooltip.
    const std::wstring disconnected = LR"({
        "speak": "Pixel 8 - Disconnected",
        "tooltip": "Battery 80% remaining"
    })";

    reading = {};
    assert(!PhoneLink::Parse(disconnected, reading));

    // Reject unrelated percentages and invalid battery levels.
    const std::wstring unrelated = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "tooltip": "Download progress 90%"
    })";
    assert(!PhoneLink::Parse(unrelated, reading));

    const std::wstring invalid = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "tooltip": "Battery 150% remaining"
    })";
    assert(!PhoneLink::Parse(invalid, reading));

    // Support whitespace around JSON separators.
    const std::wstring spaced = LR"({
        "speak" : "Phone - Connected to your PC",
        "tooltip" : "Battery 0% remaining",
        "text" : "0%"
    })";
    assert(PhoneLink::Parse(spaced, reading));
    assert(reading.percent == 0);
    assert(!reading.charging);

    // A Japanese disconnected phone must never be reported as connected.
    const std::wstring japaneseDisconnected = LR"({
        "speak": "iPhone - PC から切断されました",
        "tooltip": "バッテリー 残量 55%"
    })";

    reading = {};
    assert(!PhoneLink::Parse(japaneseDisconnected, reading));

    // Unknown localized connection messages must fail safely.
    const std::wstring unknownLocale = LR"({
        "speak": "iPhone - Connected",
        "tooltip": "Battery 55% remaining"
    })";

    assert(!PhoneLink::Parse(unknownLocale, reading));

    // An unrelated notification tooltip must not override the phone battery.
    const std::wstring unrelatedTooltip = LR"({
        "speak": "iPhone - PC に接続しました",
        "body": [
            {
                "type": "Notification",
                "tooltip": "Battery 99% remaining",
                "text": "Other device"
            },
            {
                "type": "TextBlock",
                "tooltip": "バッテリー 残量 55%",
                "text": "55%"
            }
        ]
    })";

    reading = {};
    assert(PhoneLink::Parse(unrelatedTooltip, reading));
    assert(reading.percent == 55);

    // Legacy English card with a battery tooltip but no text field.
    const std::wstring legacyEnglish = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "tooltip": "Battery 42% remaining"
    })";

    reading = {};
    assert(PhoneLink::Parse(legacyEnglish, reading));
    assert(reading.percent == 42);
    assert(!reading.charging);

    const std::wstring nestedLegacy = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "body": [
            {"tooltip": "Battery 45% remaining"}
        ]
    })";

    reading = {};
    assert(PhoneLink::Parse(nestedLegacy, reading));
    assert(reading.percent == 45);


    const std::wstring sameLanguage = LR"({"speak":"Phone - Connected to your PC","body":[{"type":"Notification","tooltip":"Battery 99% remaining"},{"type":"TextBlock","tooltip":"Battery 55% remaining","text":"55%"}]})";
    assert(PhoneLink::Parse(sameLanguage,reading) && reading.percent==55);
    const std::wstring ambiguous = LR"({"speak":"Phone - Connected to your PC","body":[{"tooltip":"Battery 99% remaining"},{"tooltip":"Battery 55% remaining"}]})";
    assert(!PhoneLink::Parse(ambiguous,reading));
    const std::wstring metadata = LR"({"speak":"Phone - Connected to your PC","metadata":{"tooltip":"Battery 99% remaining"},"body":[{"tooltip":"Battery 55% remaining"}]})";
    assert(PhoneLink::Parse(metadata,reading) && reading.percent==55);
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery 55% remaining","text":"Download complete"})",reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery 55% remaining")",reading));
    assert(!PhoneLink::Parse(legacyEnglish+L"junk",reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","speak":"other","tooltip":"Battery 55% remaining"})",reading));
    assert(PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery \u0035\u0035% remaining"})",reading) && reading.percent==55);
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery \uZZZZ% remaining"})",reading));
    std::wstring deep(70,L'[');deep+=L"0";deep+=std::wstring(70,L']');
    assert(!PhoneLink::Parse(deep,reading));

    // Other Windows languages: read from the card layout (Russian and Turkish cards, real layout).
    const std::wstring russian = LR"({
        "speak": "Galaxy S21 - Подключено к компьютеру",
        "body": [
            {"type": "ColumnSet", "tooltip": "Открыть экран телефона"},
            {"type": "TextBlock", "text": "Galaxy S21"},
            {"type": "ColumnSet", "columns": [
                {"type": "Column", "items": [{"type": "Image", "tooltip": "Подключено к компьютеру"}]},
                {"type": "Column", "items": [{"type": "Container", "tooltip": "Заряд батареи: 69%",
                    "items": [{"type": "TextBlock", "text": "69%"}]}]}
            ]}
        ]
    })";
    reading = {};
    assert(PhoneLink::Parse(russian, reading));
    assert(reading.name == L"Galaxy S21" && reading.percent == 69 && !reading.charging);

    const std::wstring turkish = LR"({"speak":"Ali - Bilgisayarınıza bağlı","body":[{"type":"TextBlock","text":"Ali"},
        {"type":"Container","tooltip":"Pil: %7 kaldı","items":[{"type":"TextBlock","text":"%7"}]}]})";
    reading = {};
    assert(PhoneLink::Parse(turkish, reading) && reading.name == L"Ali" && reading.percent == 7);

    // Layout fallback: the tooltip must carry the same number, the name must be on the card, and readings must agree.
    assert(!PhoneLink::Parse(LR"({"speak":"Ali - Bağlı","body":[{"type":"TextBlock","text":"Ali"},{"type":"Container","tooltip":"Pil 70","items":[{"type":"TextBlock","text":"7%"}]}]})", reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Ali - Bağlı","body":[{"type":"TextBlock","text":"Veli"},{"type":"Container","tooltip":"Pil 7","items":[{"type":"TextBlock","text":"7%"}]}]})", reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Ali - Bağlı","body":[{"type":"TextBlock","text":"Ali"},{"type":"Container","tooltip":"Pil 7","items":[{"type":"TextBlock","text":"7%"}]},{"type":"Container","tooltip":"Pil 9","items":[{"type":"TextBlock","text":"9%"}]}]})", reading));
    // An English or Japanese card with another status never takes the layout path.
    assert(!PhoneLink::Parse(LR"({"speak":"Pixel 8 - Disconnected","body":[{"type":"TextBlock","text":"Pixel 8"},{"type":"Container","tooltip":"Battery 80% remaining","items":[{"type":"TextBlock","text":"80%"}]}]})", reading));
    // A name containing " - " still matches.
    reading = {};
    assert(PhoneLink::Parse(LR"({"speak":"A - B - Подключено","body":[{"type":"TextBlock","text":"A - B"},{"type":"Container","tooltip":"Заряд 50 %","items":[{"type":"TextBlock","text":"50 %"}]}]})", reading) && reading.name == L"A - B" && reading.percent == 50);

    std::puts("Phone Link structural and localization tests passed");
    return 0;
}
