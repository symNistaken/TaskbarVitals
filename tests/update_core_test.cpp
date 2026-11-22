#include "../src/update_core.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace updates;

std::string Release(const std::string& tag = "v1.0.10", const std::string& assets = "[]",
                    const std::string& extra = "") {
    return "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://github.com/" +
        Repository + "/releases/tag/" + tag + "\",\"assets\":" + assets + extra + "}";
}

std::string Asset() {
    return std::string("{\"name\":\"BatteryHub-Setup-1.0.10.exe\",\"state\":\"uploaded\",\"size\":42,") +
        "\"digest\":\"sha256:" + std::string(64, 'a') + "\",\"browser_download_url\":\"https://github.com/" + Repository +
        "/releases/download/v1.0.10/BatteryHub-Setup-1.0.10.exe\"}";
}

// The signed checksum list and its signature, both required for an install.
std::string SignedAssets(const std::string& tag = "v1.0.10") {
    std::string base = std::string("https://github.com/") + Repository + "/releases/download/" + tag + "/";
    return std::string(",{\"name\":\"SHA256SUMS.txt\",\"state\":\"uploaded\",\"size\":200,\"browser_download_url\":\"") +
           base + "SHA256SUMS.txt\"},{\"name\":\"SHA256SUMS.txt.sig\",\"state\":\"uploaded\",\"size\":89," +
           "\"browser_download_url\":\"" + base + "SHA256SUMS.txt.sig\"}";
}

std::string Replace(std::string value, const std::string& before, const std::string& after) {
    auto p = value.find(before);
    assert(p != std::string::npos);
    value.replace(p, before.size(), after);
    return value;
}

int main() {
    Version current, candidate;
    assert(ParseVersion("1.0.9", current));
    assert(ParseVersion("v1.0.10", candidate) && Newer(candidate, current));
    assert(VersionText(candidate) == "1.0.10");
    assert(!Newer(current, candidate) && !Newer(current, current));
    for (const auto& invalid : { "", "v", "1", "1.2", "1.2.3.4", "1.2.3-beta", "1.2.3+build", "01.2.3",
                                "1.-2.3", "1.2.65536", "999999999999999.2.3", " 1.2.3", "1.2.3\n", "vv1.2.3" })
        assert(!ParseVersion(invalid, candidate));
    assert(ParseVersion("65535.65535.65535", candidate));
    std::string hex;
    assert(ValidDigest("sha256:" + std::string(64, 'A'), hex) && hex == std::string(64, 'a'));
    assert(!ValidDigest("sha256:" + std::string(64, 'g'), hex));
    assert(!ValidDigest(std::string(64, 'a'), hex));
    ReleaseInfo result;
    auto valid = Release("v1.0.10", "[" + Asset() + SignedAssets() + "]");
    assert(ParseRelease(valid, current, result) == ReleaseResult::Available && result.CanInstall());
    assert(result.size == 42 && result.sha256 == std::string(64, 'a'));
    assert(result.sumsSize == 200 && result.signatureSize == 89);
    assert(result.signatureUrl == std::string("https://github.com/") + Repository + "/releases/download/v1.0.10/SHA256SUMS.txt.sig");
    // Without the signature (not uploaded yet) or the checksum list, the update is shown but not installable.
    assert(ParseRelease(Release("v1.0.10", "[" + Asset() + "]"), current, result) == ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Replace(valid, "\"SHA256SUMS.txt.sig\",\"state\"", "\"other.sig\",\"state\""), current, result) ==
           ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Replace(valid, "\"name\":\"SHA256SUMS.txt\",", "\"name\":\"SUMS.txt\","), current, result) ==
           ReleaseResult::Available && !result.CanInstall());
    // Wrong URL, too big, listed twice.
    assert(ParseRelease(Replace(valid, "/v1.0.10/SHA256SUMS.txt.sig", "/v1.0.9/SHA256SUMS.txt.sig"), current, result) ==
           ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Replace(valid, "\"size\":89", "\"size\":1025"), current, result) == ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Replace(valid, "\"size\":200", "\"size\":65537"), current, result) == ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Release("v1.0.10", "[" + Asset() + SignedAssets() + SignedAssets() + "]"), current, result) ==
           ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Release("1.0.10"), current, result) == ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Release("v1.0.9"), current, result) == ReleaseResult::NoUpdate);
    assert(ParseRelease(Release("v1.0.8"), current, result) == ReleaseResult::NoUpdate);
    assert(ParseRelease(Replace(valid, "\"draft\":false", "\"draft\":true"), current, result) == ReleaseResult::NoUpdate);
    assert(ParseRelease(Replace(valid, "\"prerelease\":false", "\"prerelease\":true"), current, result) == ReleaseResult::NoUpdate);
    assert(ParseRelease(Replace(valid, "\"draft\":false", "\"draft\":\"false\""), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(Replace(valid, "\"draft\":false", "\"draft\":false,\"draft\":false"), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(Replace(valid, "https://github.com/", "https://github.com.evil/"), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(valid + " x", current, result) == ReleaseResult::Invalid);
    for (size_t cut = 0; cut < valid.size(); ++cut)
        assert(ParseRelease(valid.substr(0, cut), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(Release("v1.0.10", "[" + Asset() + "," + Asset() + SignedAssets() + "]"), current, result) == ReleaseResult::Available && !result.CanInstall());
    for (auto bad : { Replace(Asset(), "42", "0"), Replace(Asset(), "42", "104857601"), Replace(Asset(), "42", "42.0"),
                      Replace(Asset(), "42", "-42"), Replace(Asset(), "42", "4.2e1"), Replace(Asset(), "uploaded", "new"),
                      Replace(Asset(), "sha256:", "md5:"), Replace(Asset(), "https://github.com/", "http://github.com/") }) {
        assert(ParseRelease(Release("v1.0.10", "[" + bad + SignedAssets() + "]"), current, result) == ReleaseResult::Available && !result.CanInstall());
    }
    assert(ParseRelease(Replace(valid, "\"digest\":\"sha256:" + std::string(64, 'a') + "\"", "\"digest\":null"), current, result) == ReleaseResult::Available && !result.CanInstall());
    assert(ParseRelease(Release("v1.0.10", "[]", ",\"note\":\"\\uD83D\\uDE00 \\u00e9\\n\""), current, result) == ReleaseResult::Available);
    for (const auto& badString : { "\\uD800", "\\uDC00", "\\uD800\\u1234", "\\x", "\\uZZZZ" })
        assert(ParseRelease(Release("v1.0.10", "[]", std::string(",\"note\":\"") + badString + "\""), current, result) == ReleaseResult::Invalid);
    for (const auto& badLiteral : { "tru", "falsee", "nil", "01", "-", "1.", "1e", "1e+", "+1" })
        assert(ParseRelease(Release("v1.0.10", "[]", std::string(",\"note\":") + badLiteral), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(Release("v1.0.10", "[]", ",\"nested\":{\"array\":[null,true,false,1.2e+3]}"), current, result) == ReleaseResult::Available);
    assert(ParseRelease(Release("v1.0.10", "[]", ",\"deep\":" + std::string(33, '[') + "0" + std::string(33, ']')), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(std::string(1024 * 1024 + 1, ' '), current, result) == ReleaseResult::Invalid);
    assert(ParseRelease(Release("v1.0.10", "[]", ",\"note\":\"" + std::string("\xC0\x80", 2) + "\""), current, result) == ReleaseResult::Invalid);
    assert(AllowedDownloadUrl(L"https://github.com/a"));
    assert(AllowedDownloadUrl(L"https://release-assets.githubusercontent.com/a?signature=x"));
    for (const auto* url : { L"http://github.com/a", L"https://github.com.evil/a", L"https://user@github.com/a",
                             L"https://github.com:443/a", L"https://github.com/a#x", L"https://github.com/a\r\nx" })
        assert(!AllowedDownloadUrl(url));
    std::puts("Update core tests passed (versions, strict JSON, release and asset validation).");
}
