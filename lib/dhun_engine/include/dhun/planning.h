#pragma once

/* ============================================================
   PLANNING ENGINES
   Ports of engine/ledger.ts, engine/scenarios.ts,
   engine/compute.ts and engine/zoneStatus.ts. Inputs and outputs
   are the upstream object shapes, as JSON.
   ============================================================ */

#include <map>
#include <string>
#include <vector>

#include "dhun/js.h"
#include "dhun/phases.h"
#include "dhun/simulation.h"

namespace dhun {

/* ---------- ledger.ts ---------- */

/* options: { forestAcres?, gridFactor?, residualT?, horizonYears? } */
Json buildLedger(PhaseId phase, const Json& options = Json::object());

/* ---------- scenarios.ts ---------- */

Json computeLoadKw();      // COMPUTE_LOAD_KW
Json computeLabels();      // COMPUTE_LABEL
Json coolingProfiles();    // COOLING_PROFILE
Json coolingLabels();      // COOLING_LABEL

Json defaultScenario(PhaseId phase);

/* inputs: ScenarioInputs. Throws std::invalid_argument for an
   unknown cooling type (the upstream code throws a TypeError). */
Json runScenario(PhaseId phase, const Json& inputs);

/* ---------- compute.ts ---------- */

inline constexpr double KWH_PER_MILLION_TOKENS = 0.34;

double marginalIntensity(PhaseId phase, double hour);
Json computeWindows(PhaseId phase);
Json computeQueue();
double jobEnergyKwh(PhaseId phase, const Json& job);
Json jobCarbon(PhaseId phase, const Json& job, double nowHour);

/* ---------- zoneStatus.ts ---------- */

struct Pressure {
    std::string level;  // healthy | watch | attention
    std::string text;
};

using ZonePressures = std::vector<std::pair<std::string, std::vector<Pressure>>>;

std::string worst(const std::vector<std::string>& levels);
ZonePressures zonePressures(const TwinSummary& summary, const std::vector<DhunAsset>& assets);
std::string zoneLevel(const std::vector<Pressure>& pressures);
Pressure topPressure(const std::vector<Pressure>& pressures, const std::string& level);

Json toJson(const Pressure& pressure);
Json toJson(const ZonePressures& pressures);

/* The Campus workspace's issue list: every zone that is not healthy,
   attention first, with its reason and anchor asset id. */
Json zoneIssues(const TwinSummary& summary, const std::vector<DhunAsset>& assets);

/* Site-plan zones (ZONES of data/zones.ts) as JSON. */
const Json& sitePlan();

}  // namespace dhun
