#include "dhun/planning.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "dhun/catalog.h"
#include "dhun/constants.h"
#include "dhun/format.h"
#include "dhun/jsonutil.h"

namespace dhun {

/* ============================================================
   CARBON LEDGER  (engine/ledger.ts)
   ============================================================ */

Json buildLedger(PhaseId phase, const Json& options)
{
    const PhaseConfig& config = phaseConfig(phase);
    const AnnualEnergy energy = annualEnergy(phase);

    const double horizon = numberOr(options, "horizonYears", SYSTEM_LIFETIME_YR);
    const double forestAcres = numberOr(options, "forestAcres", config.forestAcres);

    const double upfrontT = (config.builtM2 * UPFRONT_KG_M2) / 1000;
    const double useEmbodiedT = (config.builtM2 * USE_EMBODIED_KG_M2) / 1000;
    const double operationalT = numberOr(options, "residualT", energy.carbonT);

    const double sinkT =
        (forestAcres / AFFORESTATION_OFFSET_ACRES) * BIO_SEQUESTRATION_T_YR + FARM_SEQUESTRATION_T_YR;

    const double netOperationalT = operationalT - sinkT;

    const double productT = upfrontT * 0.62;
    const double installT = upfrontT - productT;

    const double maintenanceT = useEmbodiedT * 0.55;
    const double endOfLifeT = useEmbodiedT - maintenanceT;

    auto line = [](const char* key, const char* label, const std::string& detail, double valueT,
                   const char* tier, const char* tone) {
        Json j;
        j["key"] = key;
        j["label"] = label;
        j["detail"] = detail;
        j["valueT"] = valueT;
        j["tier"] = tier;
        j["tone"] = tone;
        return j;
    };

    Json lines = Json::array();
    lines.push_back(line("a1-a3", "A1–A3 · Materials & construction",
                         "Cement, steel, brick, glass and finishes, cradle to gate.", productT,
                         "REPORT", "emit"));
    lines.push_back(line("a4-a5", "A4–A5 · Transport & installation",
                         "Inbound haulage, site energy and construction waste.", installT,
                         "ASSUMPTION", "emit"));
    lines.push_back(line("b1-b4", "B1–B4 · Maintenance & replacement",
                         "Refurbishment cycles, envelope repair and equipment swaps over the life of the build.",
                         maintenanceT, "ASSUMPTION", "emit"));
    lines.push_back(line("c1-c4", "C1–C4 · End of life",
                         "Demolition, haulage and disposal. Modelled, not measured.", endOfLifeT,
                         "ASSUMPTION", "emit"));
    lines.push_back(line("b6", "B6 · Operational energy",
                         !defined(options, "residualT")
                             ? "Residual grid import after on-site solar, at the Dhun grid factor."
                             : "Residual grid import after the modelled solar build.",
                         operationalT, "SIMULATED", "neutral"));
    lines.push_back(line("sink", "Sequestration · Bioreserve & farms",
                         "Additional annual uptake on the protected land. Standing biomass is held separately and is never netted off here.",
                         -sinkT, "ASSUMPTION", "sink"));

    const double wholeLifeT = upfrontT + useEmbodiedT + operationalT - sinkT;
    const double annualBudgetT = TARGET_PER_CAPITA_T * DESIGN_POPULATION;
    const double upfrontAmortisedT = (upfrontT + useEmbodiedT) / horizon;

    std::optional<double> yearsToOffsetUpfront;
    if (netOperationalT < 0) {
        yearsToOffsetUpfront = js::round((upfrontT + useEmbodiedT) / -netOperationalT);
    }

    Json j;
    j["phase"] = std::string(phaseKey(phase));
    j["lines"] = lines;
    j["upfrontT"] = upfrontT;
    j["useEmbodiedT"] = useEmbodiedT;
    j["operationalT"] = operationalT;
    j["sinkT"] = sinkT;
    j["netOperationalT"] = netOperationalT;
    j["wholeLifeT"] = wholeLifeT;
    j["perCapitaT"] = config.population > 0 ? netOperationalT / config.population : 0.0;
    j["operationalPerCapitaT"] = config.population > 0 ? operationalT / config.population : 0.0;
    j["targetPerCapitaT"] = TARGET_PER_CAPITA_T;
    j["annualBudgetT"] = annualBudgetT;
    j["upfrontAmortisedT"] = upfrontAmortisedT;
    j["upfrontVsBudget"] = upfrontAmortisedT / std::max(1.0, annualBudgetT);
    j["yearsToOffsetUpfront"] = yearsToOffsetUpfront && *yearsToOffsetUpfront > 0
                                    ? Json(*yearsToOffsetUpfront)
                                    : Json(nullptr);
    j["population"] = config.population;
    j["builtM2"] = config.builtM2;
    return j;
}

/* ============================================================
   SCENARIO ENGINE  (engine/scenarios.ts)
   ============================================================ */

namespace {

const Json& scenarioOptions()
{
    return catalog::get("scenario_options");
}

/* Capital recovery factor at 8% real over the system life. */
constexpr double DISCOUNT_RATE = 0.08;

double capitalRecoveryFactor(double rate, double years)
{
    const double growth = std::pow(1 + rate, years);
    return (rate * growth) / (growth - 1);
}

const double CRF = capitalRecoveryFactor(DISCOUNT_RATE, SYSTEM_LIFETIME_YR);

}  // namespace

Json computeLoadKw() { return scenarioOptions()["COMPUTE_LOAD_KW"]; }
Json computeLabels() { return scenarioOptions()["COMPUTE_LABEL"]; }
Json coolingProfiles() { return scenarioOptions()["COOLING_PROFILE"]; }
Json coolingLabels() { return scenarioOptions()["COOLING_LABEL"]; }

Json defaultScenario(PhaseId phase)
{
    const PhaseConfig& config = phaseConfig(phase);

    const char* compute = config.computeKw >= 28   ? "flagship"
                          : config.computeKw >= 10 ? "server"
                          : config.computeKw > 0   ? "workstation"
                                                   : "none";

    Json j;
    j["solarKwp"] = config.solarKwp;
    j["epi"] = config.epi;
    j["batteryMwh"] = js::round((config.solarKwp * 0.12) / 100) / 10;
    j["gridFactor"] = GRID_FACTOR;
    j["forestAcres"] = config.forestAcres;
    j["compute"] = compute;
    j["cooling"] = "dry";
    return j;
}

Json runScenario(PhaseId phase, const Json& inputs)
{
    const PhaseConfig& config = phaseConfig(phase);

    const std::string coolingKey = stringOr(inputs, "cooling");
    const Json& profiles = scenarioOptions()["COOLING_PROFILE"];
    if (!profiles.contains(coolingKey)) {
        throw std::invalid_argument("Unknown cooling type '" + coolingKey + "'");
    }
    const double pue = profiles[coolingKey]["pue"].get<double>();
    const double wue = profiles[coolingKey]["wue"].get<double>();

    const std::string computeKey = stringOr(inputs, "compute");
    const Json& loads = scenarioOptions()["COMPUTE_LOAD_KW"];
    const double itKw = loads.contains(computeKey) ? loads[computeKey].get<double>()
                                                   : std::numeric_limits<double>::quiet_NaN();

    const double solarKwp = numberOf(inputs, "solarKwp");
    const double epi = numberOf(inputs, "epi");
    const double batteryMwh = numberOf(inputs, "batteryMwh");
    const double gridFactor = numberOf(inputs, "gridFactor");
    const double forestAcres = numberOf(inputs, "forestAcres");

    const double computeKwhYr = itKw * pue * 24 * 365;
    const double buildingKwhYr = config.builtM2 * epi;

    const double waterKwhYr = (config.stpKlDay * STP_ENERGY_KWH_PER_KL +
                               ((config.population * LITRES_PER_PERSON_DAY) / 1000) * PUMP_ENERGY_KWH_PER_KL) *
                              365;

    const double wasteKwhYr = (0.8 + config.organicTDay * 2) * 24 * 365;
    const double spirulinaKwhYr = config.spirulinaKw * 24 * 365;

    const double campusKwhYr = buildingKwhYr + waterKwhYr + wasteKwhYr;
    const double demandKwhYr = campusKwhYr + computeKwhYr + spirulinaKwhYr;

    const double solarKwhYr = solarKwp * SPECIFIC_YIELD * SOILING_FACTOR;

    /* Math.min / Math.max propagate NaN; std::min/std::max do not. */
    auto jsMin = [](std::initializer_list<double> xs) {
        double r = std::numeric_limits<double>::infinity();
        for (double x : xs) {
            if (std::isnan(x)) return x;
            r = x < r ? x : r;
        }
        return r;
    };
    auto jsMax = [](std::initializer_list<double> xs) {
        double r = -std::numeric_limits<double>::infinity();
        for (double x : xs) {
            if (std::isnan(x)) return x;
            r = x > r ? x : r;
        }
        return r;
    };

    const double directSolar = jsMin({solarKwhYr, demandKwhYr * 0.55});
    const double surplusSolarKwh = jsMax({0, solarKwhYr - directSolar});

    const double annualBatteryThroughputKwh =
        batteryMwh * 1000 * 365 * BATTERY_ROUND_TRIP_EFFICIENCY;

    const double shiftedSolarKwh =
        jsMin({surplusSolarKwh * BATTERY_ROUND_TRIP_EFFICIENCY, annualBatteryThroughputKwh,
               jsMax({0, demandKwhYr - directSolar})});

    const double usableSolar =
        jsMin({demandKwhYr, (directSolar + shiftedSolarKwh) * SELF_CONSUMPTION_FACTOR});

    const double gridKwhYr = jsMax({0, demandKwhYr - usableSolar});

    const double renewableShare = demandKwhYr > 0 ? usableSolar / demandKwhYr : 0;

    const double residualT = (gridKwhYr * gridFactor) / 1000;

    const double sinkT =
        (forestAcres / AFFORESTATION_OFFSET_ACRES) * BIO_SEQUESTRATION_T_YR + FARM_SEQUESTRATION_T_YR;

    const double netT = residualT - sinkT;

    const double upfrontT = (config.builtM2 * UPFRONT_KG_M2) / 1000;
    const double useEmbodiedT = (config.builtM2 * USE_EMBODIED_KG_M2) / 1000;

    std::optional<double> yearsToOffset;
    if (netT < 0) yearsToOffset = js::round((upfrontT + useEmbodiedT) / -netT);

    const double domesticKlYr = (config.population * LITRES_PER_PERSON_DAY * 365) / 1000;
    const double irrigationKlYr = landscapeWaterKlYr(phase);
    const double computeKlYr = (computeKwhYr * wue) / 1000;

    const double capex = solarKwp * PV_CAPEX_INR_PER_KWP + batteryMwh * 1000 * BATTERY_CAPEX_INR_PER_KWH;
    const double annualCost = capex * CRF + capex * OANDM_FRACTION;

    std::optional<double> lcoe;
    if (solarKwhYr > 0) lcoe = annualCost / solarKwhYr;

    const double perCapitaT = config.population > 0 ? netT / config.population : 0;

    std::optional<double> paybackYear;
    if (yearsToOffset && *yearsToOffset > 0) paybackYear = phaseStartYear(phase) + *yearsToOffset;

    Json j;
    j["demandKwhYr"] = demandKwhYr;
    j["solarKwhYr"] = solarKwhYr;
    j["gridKwhYr"] = gridKwhYr;
    j["renewableShare"] = renewableShare;
    j["residualT"] = residualT;
    j["sinkT"] = sinkT;
    j["netT"] = netT;
    j["perCapitaT"] = perCapitaT;
    j["targetPerCapitaT"] = TARGET_PER_CAPITA_T;
    j["waterKlYr"] = domesticKlYr + irrigationKlYr + computeKlYr;
    j["lcoeInrPerKwh"] = nullable(lcoe);
    j["upfrontT"] = upfrontT;
    j["yearsToOffset"] =
        yearsToOffset && *yearsToOffset > 0 ? Json(*yearsToOffset) : Json(nullptr);
    j["paybackYear"] = nullable(paybackYear);
    j["offsetBy2050"] = paybackYear.has_value() && *paybackYear <= 2050;
    j["capexCr"] = capex / 1e7;
    return j;
}

/* ============================================================
   CARBON-AWARE COMPUTE  (engine/compute.ts)
   ============================================================ */

double marginalIntensity(PhaseId phase, double hour)
{
    const PhaseConfig& config = phaseConfig(phase);
    const double f = hour / 24;

    const double demandKw = (config.builtM2 * config.epi) / 8760 +
                            config.computeKw * config.computePue +
                            config.stpKlDay * 0.5 / 24 +
                            ((config.population * 78) / 1000) * 0.4 / 24 + 1;

    const double solarKw = config.solarKwp * SOLAR_PEAK_FACTOR * solarShape(f);

    const double renewableShare = clamp(solarKw / std::max(1.0, demandKw), 0, 1);

    return GRID_FACTOR * 1000 * gridIntensity(f) * (1 - renewableShare);
}

namespace {

std::string hourLabel(double hour)
{
    return js::padStart(js::toString(hour), 2, '0') + ":00";
}

}  // namespace

Json computeWindows(PhaseId phase)
{
    const PhaseConfig& config = phaseConfig(phase);
    Json windows = Json::array();
    for (int hour = 0; hour < 24; ++hour) {
        Json w;
        w["hour"] = hour;
        w["label"] = hourLabel(hour);
        w["intensity"] = marginalIntensity(phase, hour);
        w["solarKw"] = config.solarKwp * SOLAR_PEAK_FACTOR * solarShape(hour / 24.0);
        windows.push_back(w);
    }
    return windows;
}

Json computeQueue()
{
    return catalog::get("compute_queue")["QUEUE"];
}

double jobEnergyKwh(PhaseId phase, const Json& job)
{
    return numberOf(job, "tokensM") * KWH_PER_MILLION_TOKENS * phaseConfig(phase).computePue;
}

Json jobCarbon(PhaseId phase, const Json& job, double nowHour)
{
    const double energy = jobEnergyKwh(phase, job);
    const double runNow = (energy * marginalIntensity(phase, nowHour)) / 1000;

    /* Only hours still ahead in the day are schedulable. */
    double bestHour = std::fmod(nowHour + 0 + 1, 24);
    double bestValue = std::numeric_limits<double>::infinity();

    for (int offset = 0; offset < 24; ++offset) {
        const double hour = std::fmod(nowHour + offset + 1, 24);
        const double value = marginalIntensity(phase, hour);
        if (value < bestValue) {
            bestValue = value;
            bestHour = hour;
        }
    }

    const double bestWindow = (energy * bestValue) / 1000;
    const double saving = std::max(0.0, runNow - bestWindow);

    Json j;
    j["runNow"] = runNow;
    j["bestWindow"] = bestWindow;
    j["saving"] = saving;
    j["savingPercent"] = runNow > 0 ? (saving / runNow) * 100 : 0.0;
    j["bestLabel"] = hourLabel(bestHour);
    return j;
}

/* ============================================================
   ZONE PRESSURE  (engine/zoneStatus.ts)
   ============================================================ */

namespace {

int rank(const std::string& level)
{
    if (level == "attention") return 2;
    if (level == "watch") return 1;
    return 0;
}

std::vector<const DhunAsset*> byZone(const std::vector<DhunAsset>& assets, const std::string& zone)
{
    std::vector<const DhunAsset*> out;
    for (const auto& asset : assets) {
        if (asset.zone == zone) out.push_back(&asset);
    }
    return out;
}

std::vector<Pressure>& entry(ZonePressures& map, const std::string& zone)
{
    for (auto& [id, list] : map) {
        if (id == zone) return list;
    }
    map.emplace_back(zone, std::vector<Pressure>{});
    return map.back().second;
}

}  // namespace

const Json& sitePlan()
{
    return catalog::get("site_plan");
}

std::string worst(const std::vector<std::string>& levels)
{
    std::string acc = "healthy";
    for (const auto& level : levels) {
        if (rank(level) > rank(acc)) acc = level;
    }
    return acc;
}

std::string zoneLevel(const std::vector<Pressure>& pressures)
{
    std::vector<std::string> levels;
    for (const auto& p : pressures) levels.push_back(p.level);
    return worst(levels);
}

Pressure topPressure(const std::vector<Pressure>& pressures, const std::string& level)
{
    for (const auto& p : pressures) {
        if (p.level == level) return p;
    }
    return pressures.empty() ? Pressure{} : pressures.front();
}

ZonePressures zonePressures(const TwinSummary& summary, const std::vector<DhunAsset>& assets)
{
    const PhaseConfig& config = phaseConfig(summary.phase);

    const double irrigationKlYr = landscapeWaterKlYr(summary.phase);
    const double waterDemandKlYr = summary.waterKlDay * 365 + irrigationKlYr;
    const double waterRatio = waterDemandKlYr / summary.waterHarvestKlYr;

    std::vector<Pressure> energy;
    if (config.solarKwp <= 0) {
        energy.push_back({"attention",
                          "No on-site generation in this phase. Every kilowatt is imported from the grid."});
    } else if (summary.renewableShare < 0.5) {
        energy.push_back({"watch", "Solar covers " + percent(summary.renewableShare) +
                                       " of daily demand, below the 50% planning threshold."});
    } else {
        energy.push_back({"healthy", "Solar covers " + percent(summary.renewableShare) + " of daily demand."});
    }

    std::vector<Pressure> compute;
    const auto computeAssets = byZone(assets, "compute");
    if (!computeAssets.empty()) {
        const auto* computeAsset = computeAssets.front();
        if (computeAsset->status == "watch") {
            compute.push_back({"watch", "PUE is above the 1.45 design target; cooling is the gap."});
        } else if (computeAsset->status == "attention") {
            compute.push_back({"attention", "Compute is a material share of campus emissions."});
        } else {
            compute.push_back({"healthy", "Load is small, deferrable, and inside the PUE target."});
        }
    }

    std::vector<Pressure> water;
    if (waterRatio > 1) {
        water.push_back({"attention", "Annual demand of " +
                                          js::toString(js::round(waterDemandKlYr / 1000)) +
                                          "k kL exceeds the " +
                                          js::toString(js::round(summary.waterHarvestKlYr / 1000)) +
                                          "k kL harvest."});
    } else if (waterRatio > 0.8) {
        water.push_back({"watch", "Demand is at " + percent(waterRatio) +
                                      " of harvest — the buffer is thin in a dry year."});
    } else {
        water.push_back({"healthy", "Demand is " + percent(waterRatio) + " of harvested supply."});
    }

    std::vector<Pressure> built;
    {
        const auto builtAssets = byZone(assets, "built");
        std::vector<std::string> levels;
        bool anyAttention = false;
        for (const auto* asset : builtAssets) {
            levels.push_back(asset->status);
            anyAttention = anyAttention || asset->status == "attention";
        }
        built.push_back({worst(levels),
                         anyAttention ? "Occupied floor area dominates residual grid emissions."
                         : summary.phase == PhaseId::Today
                             ? "School under construction. No metered baseline exists yet."
                             : "Envelope performance is inside the efficient case."});
    }

    std::vector<Pressure> bioreserve = {{"healthy", "Protected canopy is intact and sequestering."}};

    bool wasteAttention = false;
    for (const auto* asset : byZone(assets, "waste")) {
        wasteAttention = wasteAttention || asset->status == "attention";
    }
    std::vector<Pressure> waste = {
        {wasteAttention ? "attention" : "healthy",
         "Organic waste is composted on site; methane is avoided, not emitted."}};

    ZonePressures map = {
        {"bioreserve", bioreserve}, {"energy", energy}, {"water", water},
        {"built", built},           {"waste", waste},   {"compute", compute},
    };

    /* Any zone holding a flagged asset inherits at least that level. */
    for (const auto& zone : sitePlan()["ZONES"]) {
        const std::string id = zone["id"].get<std::string>();
        const auto zoneAssets = byZone(assets, id);

        std::vector<std::string> levels;
        for (const auto* asset : zoneAssets) levels.push_back(asset->status);
        const std::string assetLevel = worst(levels);

        auto& list = entry(map, id);
        if (rank(assetLevel) > rank(zoneLevel(list))) {
            const DhunAsset* flagged = nullptr;
            for (const auto* asset : zoneAssets) {
                if (asset->status == assetLevel) {
                    flagged = asset;
                    break;
                }
            }
            list.push_back({assetLevel, flagged ? flagged->name + ": " + flagged->headlineValue + " " +
                                                      flagged->headlineLabel + "."
                                                : "Holds an asset flagged in the register."});
        }
    }

    return map;
}

Json toJson(const Pressure& pressure)
{
    Json j;
    j["level"] = pressure.level;
    j["text"] = pressure.text;
    return j;
}

Json toJson(const ZonePressures& pressures)
{
    Json j = Json::object();
    for (const auto& [zone, list] : pressures) {
        Json items = Json::array();
        for (const auto& p : list) items.push_back(toJson(p));
        j[zone] = items;
    }
    return j;
}

Json zoneIssues(const TwinSummary& summary, const std::vector<DhunAsset>& assets)
{
    const ZonePressures pressures = zonePressures(summary, assets);

    struct Issue {
        Json json;
        std::string level;
    };
    std::vector<Issue> issues;

    for (const auto& zone : sitePlan()["ZONES"]) {
        const std::string id = zone["id"].get<std::string>();
        std::vector<Pressure> list;
        for (const auto& [zid, items] : pressures) {
            if (zid == id) list = items;
        }
        const std::string level = zoneLevel(list);
        if (level == "healthy") continue;

        Json issue;
        issue["zone"] = zone;
        issue["level"] = level;
        issue["reason"] = topPressure(list, level).text;
        Json asset = nullptr;
        const std::string anchor = zone["anchorAsset"].get<std::string>();
        for (const auto& a : assets) {
            if (a.id == anchor) {
                asset = toJson(a);
                break;
            }
        }
        issue["asset"] = asset;
        issues.push_back({issue, level});
    }

    std::stable_sort(issues.begin(), issues.end(), [](const Issue& a, const Issue& b) {
        return a.level == "attention" && b.level != "attention";
    });

    Json out = Json::array();
    for (auto& issue : issues) out.push_back(std::move(issue.json));
    return out;
}

}  // namespace dhun
