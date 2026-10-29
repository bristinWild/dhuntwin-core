#pragma once

/* ============================================================
   CAMPUS TWIN SIMULATION
   Port of engine/simulation.ts (and the domain types of types.ts):
   live phase flows, the asset register, and whole-year totals.
   ============================================================ */

#include <string>
#include <vector>

#include "dhun/clock.h"
#include "dhun/js.h"
#include "dhun/phases.h"

namespace dhun {

/* ---------- small helpers (exported by simulation.ts) ---------- */

double clamp(double value, double min = 0, double max = 1);
double solarShape(double fraction);
double gridIntensity(double fraction);

/* ---------- domain types ---------- */

struct AssetMetric {
    std::string label;
    std::string value;
    std::string unit;  // empty = absent
    std::string tier;
};

struct DhunAsset {
    std::string id;
    std::string name;
    std::string shortName;
    std::string zone;
    std::string icon;
    std::string status;  // healthy | watch | attention

    double x = 0;
    double y = 0;

    double powerKw = 0;
    double energyKwhDay = 0;
    double carbonKgDay = 0;
    double waterKlDay = 0;
    double carbonShare = 0;

    std::string headlineValue;
    std::string headlineLabel;

    std::vector<AssetMetric> metrics;
    std::string note;
    std::string evidence;
};

struct TwinSummary {
    PhaseId phase = PhaseId::Today;
    SiteTime clock;

    double population = 0;
    double builtM2 = 0;

    double demandKw = 0;
    double solarKw = 0;
    double gridKw = 0;
    double batterySoc = 0;

    double workstreamCampusKw = 0;
    double workstreamCampusKwhDay = 0;
    double workstreamComputeKw = 0;
    double workstreamComputeKwhDay = 0;
    double workstreamSpirulinaKw = 0;
    double workstreamSpirulinaKwhDay = 0;

    double renewableShare = 0;
    double gridIntensity = 0;

    double demandKwhDay = 0;
    double solarKwhDay = 0;
    double gridKwhDay = 0;

    double carbonKgDay = 0;
    double sequestrationKgDay = 0;
    double netCarbonKgDay = 0;

    double waterKlDay = 0;
    double waterHarvestKlYr = 0;
    double standingStockT = 0;

    double perCapitaT = 0;
    double targetPerCapitaT = 0;
};

struct TwinSnapshot {
    TwinSummary summary;
    std::vector<DhunAsset> assets;
};

Json toJson(const AssetMetric& metric);
Json toJson(const DhunAsset& asset);
Json toJson(const TwinSummary& summary);
Json toJson(const TwinSnapshot& snapshot);

/* ---------- public entry point ---------- */

TwinSnapshot buildTwin(PhaseId phase, const SiteTime& now);

/* ---------- annual flows ---------- */

struct AnnualEnergy {
    double demandKwhYr = 0;
    double campusKwhYr = 0;
    double computeKwhYr = 0;
    double spirulinaKwhYr = 0;
    double buildingKwhYr = 0;
    double waterKwhYr = 0;
    double wasteKwhYr = 0;
    double solarKwhYr = 0;
    double gridKwhYr = 0;
    double renewableShare = 0;
    double carbonT = 0;
    double sinkT = 0;
};

Json toJson(const AnnualEnergy& energy);

double landscapeWaterKlYr(PhaseId phase);
AnnualEnergy annualEnergy(PhaseId phase);

}  // namespace dhun
