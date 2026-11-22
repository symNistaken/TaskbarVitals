// Release signature checks against a real signature from scripts/sign-release.ps1, made with a
// throwaway test key (not the release key) over the list below.
#include "../src/update_signature.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace updates;

int main() {
    const std::string key = "RUNTMSAAAAAXg7njAQm2KvYEjKiVPaIEfV2WCDfTUDss+EegIuyB0N3musI8uy6MWetKMc9YxiHaGtBCkuE9oU0M7pNnojeR";
    const std::string otherKey = "RUNTMSAAAABZ7G3LXrKOdli89SBd+tu4Sknb1Fmner6WWKLzVqER9beOAN/Iy3Y5c9e4/au3sylRb/t2Wl3yVxuijCYv5zEP";
    const std::string setupHash = "abababababababababababababababababababababababababababababababab";
    const std::string sums = setupHash + "  WirelessBattery-Setup-9.9.9.exe\n"
                             "cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd  WirelessBattery-9.9.9-portable.zip\n";
    const std::string signature = "QzIFxH0pL6rNBWejKwS7WDHz8gnieXMHSqXexlUyHQ/cLXhk3/WFN3WpzdkyR3pb8BPFPkyEExSQ4zr6y1x3Vw==\n";

    // The signature made by the PowerShell script verifies with its key, and with nothing else.
    assert(VerifySignature(sums, signature, key));
    assert(!VerifySignature(sums, signature, otherKey));
    // Any change to the list breaks it: one hash digit, the file name, an extra line.
    std::string tampered = sums;
    tampered[0] = 'b';
    assert(!VerifySignature(tampered, signature, key));
    assert(!VerifySignature(sums + "ef  evil.exe\n", signature, key));
    std::string renamed = sums;
    renamed.replace(renamed.find("9.9.9.exe"), 5, "9.9.8");
    assert(!VerifySignature(renamed, signature, key));
    // A changed or malformed signature or key.
    std::string flipped = signature;
    flipped[10] = flipped[10] == 'A' ? 'B' : 'A';
    assert(!VerifySignature(sums, flipped, key));
    assert(!VerifySignature(sums, "", key));
    assert(!VerifySignature(sums, "not base64!", key));
    assert(!VerifySignature(sums, signature.substr(0, 40), key));
    assert(!VerifySignature(sums, signature, ""));
    assert(!VerifySignature(sums, signature, key.substr(0, 40)));
    assert(!VerifySignature("", signature, key));
    assert(!VerifySignature(std::string(MaxSumsBytes + 1, 'a'), signature, key));
    assert(!VerifySignature(sums, std::string(MaxSignatureBytes + 1, 'A'), key));

    // The release key is configured, and a signature by any other key (this test key) is not a release.
    assert(ReleaseKeyConfigured());
    assert(!VerifyReleaseSignature(sums, signature));

    // Hash lookup in the list.
    std::string hex;
    assert(SignedHash(sums, "WirelessBattery-Setup-9.9.9.exe", hex) && hex == setupHash);
    assert(SignedHash(sums, "WirelessBattery-9.9.9-portable.zip", hex) && hex[0] == 'c');
    assert(SignedHash(std::string(64, 'F') + " *x.exe\r\n", "x.exe", hex) && hex == std::string(64, 'f'));
    assert(!SignedHash(sums, "WirelessBattery-Setup-9.9.8.exe", hex) && hex.empty());
    assert(!SignedHash(sums, "WirelessBattery-Setup-9.9.9.ex", hex));   // whole name only
    assert(!SignedHash(sums + sums, "WirelessBattery-Setup-9.9.9.exe", hex));  // listed twice
    assert(!SignedHash(std::string(63, 'a') + "  x.exe\n", "x.exe", hex));  // short hash
    assert(!SignedHash(std::string(64, 'g') + "  x.exe\n", "x.exe", hex));  // not hex
    assert(!SignedHash(std::string(64, 'a') + "\tx.exe\n", "x.exe", hex));  // wrong separator
    assert(!SignedHash("", "x.exe", hex));

    std::string decoded;
    assert(Base64Decode("TWE=", decoded) && decoded == "Ma");
    assert(Base64Decode("TWFu", decoded) && decoded == "Man");
    assert(!Base64Decode("TW=u", decoded) && !Base64Decode("TWF", decoded) && !Base64Decode("TW\nu", decoded));
    std::puts("Update signature tests passed (PowerShell-made signature, tampering, wrong key, checksum list).");
}
