// Portable tests validating the generated site/devices.json hardware compatibility dataset.
#include "../src/settings_port.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using settings_port::detail::JsonParser;
using settings_port::detail::JsonValue;

std::string LoadDevicesJson() {
    for (const char* path : { "site/devices.json", "../site/devices.json" }) {
        std::ifstream f(path, std::ios::binary);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return {};
}

const JsonValue* FindField(const JsonValue& obj, const char* name) {
    if (obj.type != JsonValue::Type::Object) return nullptr;
    for (const auto& [k, v] : obj.obj) {
        if (k == name) return &v;
    }
    return nullptr;
}

bool IsValidUpperHex4(const std::string& s, size_t offset) {
    for (size_t i = 0; i < 4; ++i) {
        const char c = s[offset + i];
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
        if (!ok) return false;
    }
    return true;
}

bool IsValidVidPid(const std::string& s) {
    return s.size() == 9 && s[4] == ':' && IsValidUpperHex4(s, 0) && IsValidUpperHex4(s, 5);
}

bool ContainsRawMarkdownOrHtml(const std::string& s) {
    return s.find("&amp;") != std::string::npos ||
           s.find("&lt;") != std::string::npos ||
           s.find("&gt;") != std::string::npos ||
           s.find("<br") != std::string::npos ||
           s.find("**") != std::string::npos ||
           s.find('`') != std::string::npos;
}

bool HasVidPid(const JsonValue& entry, const std::string& target) {
    const JsonValue* vp = FindField(entry, "vidPids");
    if (!vp || vp->type != JsonValue::Type::Array) return false;
    for (const auto& el : vp->arr) {
        if (el.type == JsonValue::Type::String && el.str == target) return true;
    }
    return false;
}

const JsonValue* FindVendor(const JsonValue& root, const std::string& vendor) {
    for (const auto& entry : root.arr) {
        const JsonValue* v = FindField(entry, "vendor");
        if (v && v->type == JsonValue::Type::String && v->str == vendor) return &entry;
    }
    return nullptr;
}

void TestDevicesJsonSchemaAndContents() {
    const std::string json = LoadDevicesJson();
    assert(!json.empty());
    // Must not start with UTF-8 BOM.
    assert(!(json.size() >= 3 &&
             static_cast<unsigned char>(json[0]) == 0xEF &&
             static_cast<unsigned char>(json[1]) == 0xBB &&
             static_cast<unsigned char>(json[2]) == 0xBF));

    JsonValue root;
    assert(JsonParser(json).Parse(root));
    assert(root.type == JsonValue::Type::Array);
    assert(root.arr.size() == 85);

    const std::set<std::string> validCategories = {
        "mice", "keyboards", "audio", "controllers", "mixed", "fallbacks"
    };
    const std::set<std::string> forbiddenUsagePages = {
        "FF00:0001", "FF00:0002", "FF13:0001", "FF1C:0092",
        "FF60:0061", "FF02:0002", "FFA0:0001", "FFFF:0000"
    };

    std::set<std::string> seenVendors;
    for (const auto& entry : root.arr) {
        assert(entry.type == JsonValue::Type::Object);
        assert(entry.obj.size() == 6);

        const JsonValue* vendor = FindField(entry, "vendor");
        const JsonValue* category = FindField(entry, "category");
        const JsonValue* models = FindField(entry, "models");
        const JsonValue* protocol = FindField(entry, "protocol");
        const JsonValue* vidPids = FindField(entry, "vidPids");
        const JsonValue* tested = FindField(entry, "tested");

        assert(vendor && vendor->type == JsonValue::Type::String && !vendor->str.empty());
        assert(seenVendors.insert(vendor->str).second);
        assert(!ContainsRawMarkdownOrHtml(vendor->str));

        assert(category && category->type == JsonValue::Type::String);
        assert(validCategories.count(category->str) == 1);

        assert(models && models->type == JsonValue::Type::String && !models->str.empty());
        assert(!ContainsRawMarkdownOrHtml(models->str));

        assert(protocol && protocol->type == JsonValue::Type::String && !protocol->str.empty());
        assert(!ContainsRawMarkdownOrHtml(protocol->str));

        assert(tested && tested->type == JsonValue::Type::Bool);

        assert(vidPids && vidPids->type == JsonValue::Type::Array);
        std::set<std::string> seenVp;
        for (const auto& item : vidPids->arr) {
            assert(item.type == JsonValue::Type::String);
            assert(IsValidVidPid(item.str));
            assert(seenVp.insert(item.str).second);
            assert(forbiddenUsagePages.count(item.str) == 0);
        }
    }

    const JsonValue* bitdo = FindVendor(root, "8BitDo");
    assert(bitdo);
    assert(FindField(*bitdo, "category")->str == "controllers");
    assert(FindField(*bitdo, "tested")->boolVal == true);
    assert(HasVidPid(*bitdo, "2DC8:310B"));
    assert(HasVidPid(*bitdo, "2DC8:6012"));

    const JsonValue* logitech = FindVendor(root, "Logitech");
    assert(logitech);
    assert(FindField(*logitech, "category")->str == "mixed");
    assert(HasVidPid(*logitech, "046D:C54F"));
    assert(HasVidPid(*logitech, "046D:0ADF"));

    const JsonValue* razer = FindVendor(root, "Razer");
    assert(razer);
    assert(FindField(*razer, "category")->str == "mixed");
    assert(FindField(*razer, "tested")->boolVal == true);
    assert(HasVidPid(*razer, "1532:057D"));
    assert(!HasVidPid(*razer, "1532:2020"));
    assert(!HasVidPid(*razer, "1532:2022"));
    assert(!HasVidPid(*razer, "1532:2023"));

    const JsonValue* keychron = FindVendor(root, "Keychron");
    assert(keychron);
    assert(FindField(*keychron, "category")->str == "mixed");
    assert(FindField(*keychron, "tested")->boolVal == true);
    assert(HasVidPid(*keychron, "3434:0A50"));
    assert(HasVidPid(*keychron, "3434:D028"));

    const JsonValue* steelseries = FindVendor(root, "SteelSeries");
    assert(steelseries);
    assert(HasVidPid(*steelseries, "1038:225D"));
    assert(HasVidPid(*steelseries, "1038:1878"));

    const JsonValue* attackShark = FindVendor(root, "Attack Shark & Shared Receivers");
    assert(attackShark);
    assert(HasVidPid(*attackShark, "1A2C:8FFF"));
    assert(HasVidPid(*attackShark, "1A2C:A034"));
    assert(!HasVidPid(*attackShark, "1D57:1A2C"));

    const JsonValue* sony = FindVendor(root, "Sony");
    assert(sony);
    assert(FindField(*sony, "category")->str == "mixed");
    assert(FindField(*sony, "tested")->boolVal == true);
    assert(HasVidPid(*sony, "054C:0ECC"));

    const JsonValue* crociris = FindVendor(root, "CROCIRIS / XIBERIA");
    assert(crociris && FindField(*crociris, "category")->str == "mice");
    assert(FindField(*crociris, "tested")->boolVal == true);
    assert(HasVidPid(*crociris, "36F9:AA20"));

    const JsonValue* ipi = FindVendor(root, "IPI");
    assert(ipi && FindField(*ipi, "category")->str == "mice");
    assert(FindField(*ipi, "tested")->boolVal == true);
    assert(HasVidPid(*ipi, "372E:1014"));
    assert(HasVidPid(*ipi, "372E:1056"));

    const JsonValue* soundcore = FindVendor(root, "Anker Soundcore");
    assert(soundcore && FindField(*soundcore, "category")->str == "audio");
    const JsonValue* marshall = FindVendor(root, "Marshall");
    assert(marshall && FindField(*marshall, "category")->str == "audio");
    const JsonValue* roccat = FindVendor(root, "ROCCAT");
    assert(roccat && HasVidPid(*roccat, "1E7D:3A3F"));
    const JsonValue* steam = FindVendor(root, "Valve / Steam");
    assert(steam && FindField(*steam, "category")->str == "controllers");
    assert(HasVidPid(*steam, "28DE:1142"));

    const JsonValue* fallbacks = FindVendor(root, "Universal Fallbacks");
    assert(fallbacks);
    assert(FindField(*fallbacks, "category")->str == "fallbacks");
    assert(FindField(*fallbacks, "tested")->boolVal == false);
    assert(FindField(*fallbacks, "vidPids")->arr.empty());

    // Ensure "untested" alone does not mark an entry as hardware-tested.
    const JsonValue* endorfy = FindVendor(root, "ENDORFY");
    assert(endorfy && FindField(*endorfy, "tested")->boolVal == false);
    const JsonValue* sharkoon = FindVendor(root, "Sharkoon");
    assert(sharkoon && FindField(*sharkoon, "tested")->boolVal == false);
}

}  // namespace

int main() {
    TestDevicesJsonSchemaAndContents();
    std::puts("Devices JSON compatibility dataset tests passed (85 entries, VID:PIDs, schema, cleaning).");
    return 0;
}
