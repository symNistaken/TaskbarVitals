#include "update_core.h"
#include <map>
#include <vector>

namespace updates {
bool ParseVersion(const std::string& text, Version& out) {
    size_t pos = !text.empty() && text[0] == 'v' ? 1 : 0;
    unsigned parts[3]{};
    for (unsigned i = 0; i < 3; ++i) {
        size_t start = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            unsigned digit = (unsigned)(text[pos++] - '0');
            if (parts[i] > (65535U - digit) / 10) return false;
            parts[i] = parts[i] * 10 + digit;
        }
        if (start == pos || (pos - start > 1 && text[start] == '0')) return false;
        if (i != 2 && (pos >= text.size() || text[pos++] != '.')) return false;
    }
    if (pos != text.size()) return false;
    out = { parts[0], parts[1], parts[2] };
    return true;
}

bool Newer(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major > b.major;
    if (a.minor != b.minor) return a.minor > b.minor;
    return a.patch > b.patch;
}

std::string VersionText(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

bool ValidDigest(const std::string& digest, std::string& hex) {
    hex.clear();
    if (digest.size() != 71 || digest.compare(0, 7, "sha256:") != 0) return false;
    for (size_t i = 7; i < digest.size(); ++i) {
        char c = digest[i];
        if (c >= 'A' && c <= 'F') c += 'a' - 'A';
        if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) return false;
        hex += c;
    }
    return true;
}

bool AllowedDownloadUrl(const std::wstring& url) {
    // Exact hosts only, including the slash: no userinfo, port, or lookalike domains.
    for (const wchar_t* host : { L"https://github.com/", L"https://release-assets.githubusercontent.com/",
                                 L"https://objects.githubusercontent.com/" }) {
        if (url.compare(0, std::char_traits<wchar_t>::length(host), host) == 0 &&
            url.find(L'\r') == std::wstring::npos && url.find(L'\n') == std::wstring::npos &&
            url.find(L'#') == std::wstring::npos) return true;
    }
    return false;
}

bool CheckSchedule::Due(std::uint64_t now) const {
    // A clock moved backwards must not disable checking indefinitely.
    return now >= nextAutomatic || nextAutomatic - now > 24 * 60 * 60;
}
bool CheckSchedule::ManualAllowed(std::uint64_t now) const {
    bool serverAllows = now >= retryAfter || retryAfter - now > 24 * 60 * 60;
    return serverAllows && (!lastAttempt || now < lastAttempt || now - lastAttempt >= 60);
}
void CheckSchedule::Finished(std::uint64_t now, bool success, std::uint64_t serverRetry) {
    lastAttempt = now;
    retryAfter = serverRetry > now ? serverRetry : 0;
    nextAutomatic = now + (success ? 24 * 60 * 60 : 60 * 60);
    if (retryAfter > nextAutomatic) nextAutomatic = retryAfter;
}

namespace {
struct Json {
    enum Type { Null, Boolean, Number, String, Array, Object } type = Null;
    bool boolean = false;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
    const Json* Get(const char* key, Type expected) const {
        auto it = object.find(key);
        return type == Object && it != object.end() && it->second.type == expected ? &it->second : nullptr;
    }
};

class Parser {
    const std::string& input;
    size_t pos = 0;
    void Space() { while (pos < input.size() && (input[pos] == ' ' || input[pos] == '\t' || input[pos] == '\r' || input[pos] == '\n')) ++pos; }
    bool Take(char c) { if (pos < input.size() && input[pos] == c) { ++pos; return true; } return false; }
    bool Hex4(unsigned& n) {
        n = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos == input.size()) return false;
            char c = input[pos++];
            unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                         c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (d == 16) return false;
            n = n * 16 + d;
        }
        return true;
    }
    static void Utf8(std::string& out, unsigned n) {
        if (n < 0x80) out += (char)n;
        else if (n < 0x800) { out += (char)(0xC0 | (n >> 6)); out += (char)(0x80 | (n & 63)); }
        else if (n < 0x10000) { out += (char)(0xE0 | (n >> 12)); out += (char)(0x80 | ((n >> 6) & 63)); out += (char)(0x80 | (n & 63)); }
        else { out += (char)(0xF0 | (n >> 18)); out += (char)(0x80 | ((n >> 12) & 63)); out += (char)(0x80 | ((n >> 6) & 63)); out += (char)(0x80 | (n & 63)); }
    }
    bool String(std::string& out) {
        if (!Take('"')) return false;
        while (pos < input.size()) {
            unsigned char c = (unsigned char)input[pos++];
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c == '\\') {
                if (pos == input.size()) return false;
                char escape = input[pos++];
                switch (escape) {
                    case '"': case '\\': case '/': out += escape; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned n;
                        if (!Hex4(n)) return false;
                        if (n >= 0xD800 && n <= 0xDBFF) {
                            unsigned low;
                            if (!Take('\\') || !Take('u') || !Hex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
                            n = 0x10000 + ((n - 0xD800) << 10) + low - 0xDC00;
                        } else if (n >= 0xDC00 && n <= 0xDFFF) return false;
                        Utf8(out, n);
                        break;
                    }
                    default: return false;
                }
            } else if (c < 0x80) out += (char)c;
            else {
                unsigned count = c >= 0xC2 && c <= 0xDF ? 1 : c >= 0xE0 && c <= 0xEF ? 2 : c >= 0xF0 && c <= 0xF4 ? 3 : 0;
                if (!count || input.size() - pos < count) return false;
                unsigned n = c & (0x7F >> count);
                for (unsigned i = 0; i < count; ++i) {
                    unsigned char next = (unsigned char)input[pos++];
                    if ((next & 0xC0) != 0x80) return false;
                    n = (n << 6) | (next & 63);
                }
                if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDFFF) ||
                    n < (count == 1 ? 0x80U : count == 2 ? 0x800U : 0x10000U)) return false;
                Utf8(out, n);
            }
        }
        return false;
    }
    bool Value(Json& out, unsigned depth) {
        if (depth > 32) return false;
        Space();
        if (pos == input.size()) return false;
        if (Take('{')) {
            out.type = Json::Object;
            Space();
            if (Take('}')) return true;
            do {
                Space();
                std::string key;
                if (!String(key)) return false;
                Space();
                if (!Take(':') || out.object.count(key)) return false;
                if (!Value(out.object[key], depth + 1)) return false;
                Space();
                if (Take('}')) return true;
            } while (Take(','));
            return false;
        }
        if (Take('[')) {
            out.type = Json::Array;
            Space();
            if (Take(']')) return true;
            do {
                out.array.emplace_back();
                if (!Value(out.array.back(), depth + 1)) return false;
                Space();
                if (Take(']')) return true;
            } while (Take(','));
            return false;
        }
        if (input[pos] == '"') { out.type = Json::String; return String(out.text); }
        for (const char* literal : { "true", "false", "null" }) {
            size_t n = std::char_traits<char>::length(literal);
            if (input.compare(pos, n, literal) == 0) {
                out.type = literal[0] == 'n' ? Json::Null : Json::Boolean;
                out.boolean = literal[0] == 't';
                pos += n;
                return true;
            }
        }
        size_t start = pos;
        Take('-');
        if (!Take('0')) {
            if (pos == input.size() || input[pos] < '1' || input[pos] > '9') return false;
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
        }
        if (Take('.')) {
            size_t digits = pos;
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
            if (pos == digits) return false;
        }
        if (Take('e') || Take('E')) {
            if (!Take('+')) Take('-');
            size_t digits = pos;
            while (pos < input.size() && input[pos] >= '0' && input[pos] <= '9') ++pos;
            if (pos == digits) return false;
        }
        out.type = Json::Number;
        out.text = input.substr(start, pos - start);
        return true;
    }
public:
    explicit Parser(const std::string& text) : input(text) {}
    bool Parse(Json& out) { if (input.size() > 1024 * 1024 || !Value(out, 0)) return false; Space(); return pos == input.size(); }
};

bool AssetSize(const Json& value, std::uint64_t& out) {
    if (value.text.empty()) return false;
    out = 0;
    for (char c : value.text) {
        if (c < '0' || c > '9' || out > (MaxDownloadBytes - (unsigned)(c - '0')) / 10) return false;
        out = out * 10 + c - '0';
    }
    return out > 0 && out <= MaxDownloadBytes;
}
}

ReleaseResult ParseRelease(const std::string& json, const Version& current, ReleaseInfo& out) {
    out = {};
    Json root;
    if (!Parser(json).Parse(root)) return ReleaseResult::Invalid;
    const Json* tag = root.Get("tag_name", Json::String);
    const Json* draft = root.Get("draft", Json::Boolean);
    const Json* pre = root.Get("prerelease", Json::Boolean);
    const Json* page = root.Get("html_url", Json::String);
    const Json* assets = root.Get("assets", Json::Array);
    if (!tag || !draft || !pre || !page || !assets || !ParseVersion(tag->text, out.version)) return ReleaseResult::Invalid;
    out.tag = tag->text;
    out.page = std::string("https://github.com/") + Repository + "/releases/tag/" + out.tag;
    if (out.page != page->text) { out = {}; return ReleaseResult::Invalid; }
    if (draft->boolean || pre->boolean || !Newer(out.version, current)) { out = {}; return ReleaseResult::NoUpdate; }
    std::string name = "TaskbarVitals-Setup-" + VersionText(out.version) + ".exe";
    std::string base = std::string("https://github.com/") + Repository + "/releases/download/" + out.tag + "/";
    unsigned matches = 0, sumsMatches = 0, signatureMatches = 0;
    for (const Json& asset : assets->array) {
        const Json* assetName = asset.Get("name", Json::String);
        if (!assetName) continue;
        // The signed checksum list and its signature: small files at their exact release URLs.
        for (bool signature : { false, true }) {
            const char* file = signature ? "SHA256SUMS.txt.sig" : "SHA256SUMS.txt";
            if (assetName->text != file) continue;
            ++(signature ? signatureMatches : sumsMatches);
            const Json* state = asset.Get("state", Json::String);
            const Json* url = asset.Get("browser_download_url", Json::String);
            const Json* size = asset.Get("size", Json::Number);
            std::uint64_t bytes = 0;
            if (state && state->text == "uploaded" && url && url->text == base + file && size && AssetSize(*size, bytes) &&
                bytes <= (signature ? 1024u : 64u * 1024)) {
                (signature ? out.signatureUrl : out.sumsUrl) = url->text;
                (signature ? out.signatureSize : out.sumsSize) = bytes;
            }
        }
        if (assetName->text != name) continue;
        ++matches;
        const Json* state = asset.Get("state", Json::String);
        const Json* url = asset.Get("browser_download_url", Json::String);
        const Json* digest = asset.Get("digest", Json::String);
        const Json* size = asset.Get("size", Json::Number);
        std::string expected = std::string("https://github.com/") + Repository + "/releases/download/" + out.tag + "/" + name;
        std::string hash;
        std::uint64_t bytes = 0;
        if (state && state->text == "uploaded" && url && url->text == expected &&
            digest && ValidDigest(digest->text, hash) && size && AssetSize(*size, bytes)) {
            out.download = url->text;
            out.sha256 = hash;
            out.size = bytes;
        }
    }
    if (matches != 1) { out.download.clear(); out.sha256.clear(); out.size = 0; }
    if (sumsMatches != 1) { out.sumsUrl.clear(); out.sumsSize = 0; }
    if (signatureMatches != 1) { out.signatureUrl.clear(); out.signatureSize = 0; }
    return ReleaseResult::Available;
}
}
