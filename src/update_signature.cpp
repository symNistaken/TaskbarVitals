#include "update_signature.h"
#include "update_public_key.h"
#include "../build/version.h"

#include <windows.h>
#include <bcrypt.h>
#include <vector>

static_assert(!WB_OFFICIAL_RELEASE || updates::ReleaseKeys[0][0] != '\0',
              "Official builds need a release public key: run scripts/new-release-key.ps1");

namespace updates {
namespace {
constexpr ULONG kEcsPublicMagic = 0x31534345;  // BCRYPT_ECDSA_PUBLIC_P256_MAGIC ("ECS1")

// BCryptCreateHash / HashData / FinishHash rather than the one-call BCryptHash: that one is Windows 10
// only and missing from headers that target older Windows (the CI test build, without _WIN32_WINNT).
bool Sha256(const std::string& data, unsigned char (&digest)[32]) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0 &&
              BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) >= 0 &&
              BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}
}  // namespace

bool Base64Decode(const std::string& text, std::string& out) {
    out.clear();
    if (text.empty() || text.size() % 4) return false;
    unsigned buffer = 0;
    int bits = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        int v = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 :
                c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : c == '=' ? -2 : -1;
        if (v == -1) return false;
        if (v == -2) {  // padding: only the last one or two characters
            if (i < text.size() - 2 || (i == text.size() - 2 && text.back() != '=')) return false;
            break;
        }
        buffer = (buffer << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((buffer >> bits) & 0xFF);
        }
    }
    return true;
}

bool VerifySignature(const std::string& data, const std::string& signatureText, const std::string& keyBase64) {
    std::string key, signature;
    if (data.empty() || data.size() > MaxSumsBytes || signatureText.size() > MaxSignatureBytes) return false;
    if (!Base64Decode(Trim(keyBase64), key) || key.size() != 8 + 64) return false;
    if (!Base64Decode(Trim(signatureText), signature) || signature.size() != 64) return false;
    ULONG magic = 0, length = 0;
    memcpy(&magic, key.data(), 4);
    memcpy(&length, key.data() + 4, 4);
    if (magic != kEcsPublicMagic || length != 32) return false;
    unsigned char digest[32];
    if (!Sha256(data, digest)) return false;
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE handle = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0 &&
              BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &handle, (PUCHAR)key.data(), (ULONG)key.size(), 0) >= 0 &&
              BCryptVerifySignature(handle, nullptr, digest, sizeof(digest), (PUCHAR)signature.data(),
                                    (ULONG)signature.size(), 0) >= 0;
    if (handle) BCryptDestroyKey(handle);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool ReleaseKeyConfigured() {
    for (const char* key : ReleaseKeys)
        if (key && *key) return true;
    return false;
}

bool VerifyReleaseSignature(const std::string& data, const std::string& signatureText) {
    for (const char* key : ReleaseKeys)
        if (key && *key && VerifySignature(data, signatureText, key)) return true;
    return false;
}

bool SignedHash(const std::string& sums, const std::string& fileName, std::string& hex) {
    hex.clear();
    if (sums.empty() || sums.size() > MaxSumsBytes || fileName.empty()) return false;
    unsigned matches = 0;
    size_t start = 0;
    while (start < sums.size()) {
        size_t end = sums.find('\n', start);
        std::string line = sums.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? sums.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // 64 hex digits, a space, then a space (text mode) or '*' (binary mode), then the name.
        if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) return false;
        std::string digest;
        for (size_t i = 0; i < 64; ++i) {
            char c = line[i];
            if (c >= 'A' && c <= 'F') c += 'a' - 'A';
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            digest += c;
        }
        if (line.compare(66, std::string::npos, fileName) == 0) {
            ++matches;
            hex = digest;
        }
    }
    if (matches != 1) hex.clear();
    return matches == 1;
}
}  // namespace updates
