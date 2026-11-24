// i18n keys must not depend on leading/trailing whitespace: the parser trims every key and value,
// so such a key could never match what T() is asked for.
#include "../src/i18n.cpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static void UseText(const std::string& text) {
    g_sources.clear();
    AddSource(text, L"xx");
    g_choice = L"xx";
    Apply();
}

int main() {
    // The phrases the dashboard looks up (keys carry no edge whitespace).
    UseText("@code = xx\n"
            "Estimated full-charge runtime: {runtime} = RT {runtime}\n"
            "({full}/{design} mWh) = [{full}|{design}]\n");
    assert(TF(L"Estimated full-charge runtime: {runtime}", { { L"runtime", L"Collecting data" } }) == L"RT Collecting data");
    assert(TF(L"({full}/{design} mWh)", { { L"full", L"1" }, { L"design", L"2" } }) == L"[1|2]");

    // No key in the template or any language file may start or end with whitespace.
    int bad = 0;
    for (const auto& e : std::filesystem::directory_iterator("lang")) {
        if (e.path().extension() != ".txt") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            size_t eq = line.find(" = ");
            if (eq == std::string::npos) {
                if (line.size() >= 2 && line.compare(line.size() - 2, 2, " =") == 0) eq = line.size() - 2;  // empty value
                else continue;
            }
            std::string key = line.substr(0, eq);
            if (!key.empty() && (key.front() == ' ' || key.back() == ' ')) {
                std::printf("edge whitespace in key: %s: [%s]\n", e.path().string().c_str(), key.c_str());
                bad++;
            }
        }
    }
    assert(bad == 0);
    std::puts("i18n edge-space tests passed");
    return 0;
}
