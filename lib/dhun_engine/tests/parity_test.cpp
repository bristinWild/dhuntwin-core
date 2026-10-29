/* ============================================================
   PARITY TEST
   Replays the inputs recorded in tests/golden/*.json (produced by
   scripts/golden.ts from the ORIGINAL TypeScript engine) through
   dhun_engine and compares every field.
   ============================================================ */

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>

#include "compare.h"
#include "parity_cases.h"

namespace fs = std::filesystem;
using namespace dhun;

int main(int argc, char** argv)
{
    const fs::path dir = argc > 1 ? fs::path(argv[1]) : fs::path(DHUN_GOLDEN_DIR);
    const std::string only = argc > 2 ? argv[2] : "";

    int failed = 0;
    int total = 0;

    for (const auto& [name, run] : test::parityCases()) {
        if (!only.empty() && name != only) continue;

        const fs::path file = dir / (name + ".json");
        if (!fs::exists(file)) {
            std::cerr << "[skip] " << name << " (no golden file)\n";
            continue;
        }

        std::ifstream in(file);
        const Json golden = Json::parse(in);

        test::Report report;
        run(golden, report);

        ++total;
        if (report.failures > 0) {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << report.failures << " of " << report.checks
                      << " checks\n";
        } else {
            std::cout << "[ ok ] " << name << ": " << report.checks << " checks\n";
        }
    }

    std::cout << (failed == 0 ? "parity: all " : "parity: ") << total - failed << "/" << total
              << " suites match the TypeScript engine\n";
    return failed == 0 ? 0 : 1;
}
