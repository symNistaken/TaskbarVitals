#pragma once
// Public keys allowed to sign releases (see update_signature.h). Written by
// scripts/new-release-key.ps1; the matching private key stays on the maintainer's PC.
// More than one key lets a new key be added before an old one is retired.
// Empty means no key yet: official builds then refuse to compile, and updates cannot be installed.
namespace updates {
inline constexpr const char* ReleaseKeys[] = {
    "RUNTMSAAAABc071cP8gJE/99LTcB7rNFA5zJK3MKh4tuOw20kLw/LsCyWxTc99biMdT9tpxOsVuxjyMxYBr7Gl8YavQ+krBk",
};
}
