// Regression checks for winget and Scoop packaging manifest templates and helper scripts.
#include "../src/settings_port.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

std::string ReadText(const char* path) {
    std::ifstream f(path, std::ios::binary);
    assert(f.good());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool IsHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (unsigned char c : s) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

std::string ExtractIssAppId(const std::string& iss) {
    const size_t pos = iss.find("AppId={{");
    assert(pos != std::string::npos);
    const size_t start = pos + 7;  // starts with first '{' inside "{{...}"
    const size_t end = iss.find('}', start);
    assert(end != std::string::npos);
    return iss.substr(start, end - start + 1);
}

std::string ExtractYamlValue(const std::string& yaml, const std::string& keyPrefix) {
    size_t pos = 0;
    while (pos < yaml.size()) {
        size_t eol = yaml.find('\n', pos);
        if (eol == std::string::npos) eol = yaml.size();
        std::string line = yaml.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(keyPrefix, 0) == 0) {
            std::string val = line.substr(keyPrefix.size());
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
            if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'') {
                val = val.substr(1, val.size() - 2);
            }
            return val;
        }
        pos = eol + 1;
    }
    return "";
}

const settings_port::detail::JsonValue* FindObjKey(const settings_port::detail::JsonValue& obj, const std::string& key) {
    if (obj.type != settings_port::detail::JsonValue::Type::Object) return nullptr;
    for (const auto& [k, v] : obj.obj) {
        if (k == key) return &v;
    }
    return nullptr;
}

void TestScoopManifest() {
    using namespace settings_port::detail;
    const std::string raw = ReadText("installer/packaging/scoop/taskbar-vitals.json");
    JsonValue root;
    assert(JsonParser(raw).Parse(root));
    assert(root.type == JsonValue::Type::Object);

    const auto* ver = FindObjKey(root, "version");
    const auto* hp = FindObjKey(root, "homepage");
    const auto* lic = FindObjKey(root, "license");
    const auto* bin = FindObjKey(root, "bin");
    const auto* arch = FindObjKey(root, "architecture");
    const auto* x64 = arch ? FindObjKey(*arch, "64bit") : nullptr;
    const auto* url = x64 ? FindObjKey(*x64, "url") : nullptr;
    const auto* hash = x64 ? FindObjKey(*x64, "hash") : nullptr;
    const auto* checkver = FindObjKey(root, "checkver");
    const auto* autoupdate = FindObjKey(root, "autoupdate");

    assert(ver && ver->type == JsonValue::Type::String && ver->str == "2.1.0");
    assert(hp && hp->type == JsonValue::Type::String && hp->str == "https://github.com/symNistaken/TaskbarVitals");
    assert(lic && lic->type == JsonValue::Type::String && lic->str == "GPL-3.0-or-later");
    assert(bin && bin->type == JsonValue::Type::String && bin->str == "TaskbarVitals.exe");
    assert(url && url->type == JsonValue::Type::String &&
           url->str == "https://github.com/symNistaken/TaskbarVitals/releases/download/v2.1.0/TaskbarVitals-2.1.0-portable.zip");
    assert(hash && hash->type == JsonValue::Type::String && IsHex64(hash->str));
    assert(checkver && checkver->type == JsonValue::Type::String && checkver->str == "github");
    assert(autoupdate && autoupdate->type == JsonValue::Type::Object);
}

void TestWingetManifests() {
    const std::string iss = ReadText("installer/TaskbarVitals.iss");
    const std::string expectedProductCode = ExtractIssAppId(iss) + "_is1";

    const std::string verYaml = ReadText(
        "installer/packaging/winget/manifests/s/symNistaken/TaskbarVitals/2.1.0/symNistaken.TaskbarVitals.yaml");
    const std::string instYaml = ReadText(
        "installer/packaging/winget/manifests/s/symNistaken/TaskbarVitals/2.1.0/symNistaken.TaskbarVitals.installer.yaml");
    const std::string locYaml = ReadText(
        "installer/packaging/winget/manifests/s/symNistaken/TaskbarVitals/2.1.0/symNistaken.TaskbarVitals.locale.en-US.yaml");

    assert(ExtractYamlValue(verYaml, "PackageIdentifier:") == "symNistaken.TaskbarVitals");
    assert(ExtractYamlValue(verYaml, "PackageVersion:") == "2.1.0");
    assert(ExtractYamlValue(verYaml, "DefaultLocale:") == "en-US");
    assert(ExtractYamlValue(verYaml, "ManifestType:") == "version");
    assert(ExtractYamlValue(verYaml, "ManifestVersion:") == "1.6.0");

    assert(ExtractYamlValue(instYaml, "PackageIdentifier:") == "symNistaken.TaskbarVitals");
    assert(ExtractYamlValue(instYaml, "PackageVersion:") == "2.1.0");
    assert(ExtractYamlValue(instYaml, "InstallerType:") == "inno");
    assert(ExtractYamlValue(instYaml, "Scope:") == "user");
    assert(ExtractYamlValue(instYaml, "UpgradeBehavior:") == "install");
    assert(ExtractYamlValue(instYaml, "ManifestType:") == "installer");
    assert(ExtractYamlValue(instYaml, "ManifestVersion:") == "1.6.0");
    assert(ExtractYamlValue(instYaml, "  ProductCode:") == expectedProductCode);
    assert(ExtractYamlValue(instYaml, "  InstallerUrl:") ==
           "https://github.com/symNistaken/TaskbarVitals/releases/download/v2.1.0/TaskbarVitals-Setup-2.1.0.exe");
    assert(IsHex64(ExtractYamlValue(instYaml, "  InstallerSha256:")));

    assert(ExtractYamlValue(locYaml, "PackageIdentifier:") == "symNistaken.TaskbarVitals");
    assert(ExtractYamlValue(locYaml, "PackageVersion:") == "2.1.0");
    assert(ExtractYamlValue(locYaml, "PackageLocale:") == "en-US");
    assert(ExtractYamlValue(locYaml, "Publisher:") == "symNistaken");
    assert(ExtractYamlValue(locYaml, "PackageName:") == "Taskbar Vitals");
    assert(ExtractYamlValue(locYaml, "License:") == "GPL-3.0-or-later");
    assert(ExtractYamlValue(locYaml, "ManifestType:") == "defaultLocale");
    assert(ExtractYamlValue(locYaml, "ManifestVersion:") == "1.6.0");
}

void TestHelperScriptAndDocs() {
    const std::string ps1 = ReadText("scripts/update-packaging-manifests.ps1");
    assert(ps1.find("SHA256SUMS.txt") != std::string::npos);
    assert(ps1.find("TaskbarVitals-Setup-") != std::string::npos);
    assert(ps1.find("TaskbarVitals-") != std::string::npos);

    const std::string doc = ReadText("installer/packaging/README.md");
    assert(doc.find("SHA256SUMS.txt") != std::string::npos);
    assert(doc.find("update-packaging-manifests.ps1") != std::string::npos);
}

}  // namespace

int main() {
    TestScoopManifest();
    TestWingetManifests();
    TestHelperScriptAndDocs();
    std::puts("Packaging manifest template tests passed.");
    return 0;
}
