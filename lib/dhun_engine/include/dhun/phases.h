#pragma once

/* ============================================================
   PHASES
   Port of data/phases.ts: today, Phase 1 (school + R1), and full
   build-out. Cross-checked against data/phases.json by the tests.
   ============================================================ */

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "dhun/js.h"

namespace dhun {

enum class PhaseId { Today, Phase1, Buildout };

struct PhaseConfig {
    PhaseId id;
    const char* key;
    const char* label;
    const char* shortLabel;
    const char* description;
    double population;
    double builtM2;
    double epi;
    double solarKwp;
    double computeKw;
    double computePue;
    double spirulinaKw;
    double stpKlDay;
    double organicTDay;
    double harvestKlYr;
    double forestAcres;
    double treeCount;
};

const PhaseConfig& phaseConfig(PhaseId id);

/* "today" | "phase1" | "buildout" */
std::string_view phaseKey(PhaseId id);
std::optional<PhaseId> parsePhase(std::string_view key);

inline constexpr std::array<PhaseId, 3> PHASE_ORDER = {
    PhaseId::Today, PhaseId::Phase1, PhaseId::Buildout};

/* Calendar year each phase is planned to open. */
int phaseStartYear(PhaseId id);

/* Monthly solar seasonality for Jaipur — planning assumption. */
inline constexpr std::array<double, 12> MONTH_FACTOR = {
    0.88, 0.95, 1.02, 1.08, 1.1, 1.02,
    0.88, 0.86, 0.92, 0.98, 0.92, 0.86};

/* PhaseConfig as the upstream object literal. */
Json phaseToJson(const PhaseConfig& config);

}  // namespace dhun
