#include "dhun/simulation.h"

#include <algorithm>
#include <cmath>

#include "dhun/constants.h"

namespace dhun {

namespace {

constexpr double PI = 3.141592653589793;

/* Occupancy-driven building load shape */
double buildingShape(double fraction)
{
    return 0.58 +
           0.78 * std::exp(-std::pow((fraction - 0.6) / 0.15, 2)) +
           0.22 * std::exp(-std::pow((fraction - 0.82) / 0.09, 2));
}

double computeShape(double fraction)
{
    return 0.88 + 0.1 * std::sin(fraction * PI * 2 - 1);
}

double seasonalFactor(const SiteTime& date)
{
    return MONTH_FACTOR[static_cast<std::size_t>(date.month)];
}

/* value.toLocaleString("en-IN", { min = max = decimals }) */
std::string round(double value, int decimals = 0)
{
    return js::formatNumber(value, decimals, decimals);
}

struct PhaseFlows {
    const PhaseConfig* config = nullptr;
    double solarKw = 0;
    double solarKwhDay = 0;

    double buildingKw = 0;
    double buildingKwhDay = 0;

    double computeKw = 0;
    double computeKwhDay = 0;

    double workstreamCampusKw = 0;
    double workstreamCampusKwhDay = 0;
    double workstreamComputeKw = 0;
    double workstreamComputeKwhDay = 0;
    double workstreamSpirulinaKw = 0;
    double workstreamSpirulinaKwhDay = 0;

    double spirulinaKw = 0;
    double spirulinaKwhDay = 0;

    double stpKw = 0;
    double stpKwhDay = 0;

    double pumpKw = 0;
    double pumpKwhDay = 0;

    double wasteKw = 0;
    double wasteKwhDay = 0;

    double demandKw = 0;
    double demandKwhDay = 0;

    double gridKw = 0;
    double gridKwhDay = 0;

    double batterySoc = 0;
    double renewableShare = 0;
    double carbonKgDay = 0;
    double sequestrationKgDay = 0;
    double carbonFactor = 0;
};

PhaseFlows phaseFlows(PhaseId phase, const SiteTime& now)
{
    const PhaseConfig& config = phaseConfig(phase);
    const double f = dayFraction(now);
    const double seasonal = seasonalFactor(now);

    PhaseFlows flows;
    flows.config = &config;

    flows.solarKw = config.solarKwp * SOLAR_PEAK_FACTOR * seasonal * SOILING_FACTOR * solarShape(f);

    flows.solarKwhDay = (config.solarKwp * SPECIFIC_YIELD) / 365 * seasonal * SOILING_FACTOR;

    flows.buildingKwhDay = (config.builtM2 * config.epi) / 365;

    flows.buildingKw = (config.builtM2 * config.epi) / 8760 * buildingShape(f);

    flows.computeKwhDay = config.computeKw * config.computePue * 24;

    flows.computeKw = config.computeKw * config.computePue * computeShape(f);

    flows.stpKwhDay = config.stpKlDay * STP_ENERGY_KWH_PER_KL;
    flows.stpKw = flows.stpKwhDay / 24;

    flows.pumpKwhDay = ((config.population * LITRES_PER_PERSON_DAY) / 1000) * PUMP_ENERGY_KWH_PER_KL;
    flows.pumpKw = flows.pumpKwhDay / 24;

    flows.wasteKw = 0.8 + config.organicTDay * 2;
    flows.wasteKwhDay = flows.wasteKw * 24;

    /* Workstream 1: campus & construction */
    flows.workstreamCampusKw = flows.buildingKw + flows.stpKw + flows.pumpKw + flows.wasteKw;
    flows.workstreamCampusKwhDay =
        flows.buildingKwhDay + flows.stpKwhDay + flows.pumpKwhDay + flows.wasteKwhDay;

    /* Workstream 2: compute commons */
    flows.workstreamComputeKw = flows.computeKw;
    flows.workstreamComputeKwhDay = flows.computeKwhDay;

    /* Workstream 3: spirulina biomass pilot */
    flows.spirulinaKwhDay = config.spirulinaKw * 24;
    flows.spirulinaKw = config.spirulinaKw * (0.85 + 0.15 * std::sin(f * PI * 2));
    flows.workstreamSpirulinaKw = flows.spirulinaKw;
    flows.workstreamSpirulinaKwhDay = flows.spirulinaKwhDay;

    flows.demandKw =
        flows.workstreamCampusKw + flows.workstreamComputeKw + flows.workstreamSpirulinaKw;

    flows.demandKwhDay = flows.workstreamCampusKwhDay + flows.workstreamComputeKwhDay +
                         flows.workstreamSpirulinaKwhDay;

    flows.gridKw = std::max(0.0, flows.demandKw - flows.solarKw);
    flows.gridKwhDay = std::max(0.0, flows.demandKwhDay - flows.solarKwhDay);

    flows.renewableShare =
        flows.demandKwhDay > 0 ? std::min(1.0, flows.solarKwhDay / flows.demandKwhDay) : 0;

    flows.carbonFactor =
        flows.demandKwhDay > 0 ? (flows.gridKwhDay / flows.demandKwhDay) * GRID_FACTOR : 0;

    flows.carbonKgDay = flows.gridKwhDay * GRID_FACTOR;
    flows.sequestrationKgDay = (TOTAL_SEQUESTRATION_T_YR * 1000) / 365;

    flows.batterySoc = clamp(0.5 + 0.32 * std::sin((f - 0.22) * PI * 2), 0.12, 0.96);

    return flows;
}

std::string assetStatus(double carbonShare, bool extraWatch = false)
{
    if (extraWatch) return "watch";
    if (carbonShare > 0.45) return "attention";
    if (carbonShare > 0.25) return "watch";
    return "healthy";
}

AssetMetric m(std::string label, std::string value, std::string unit, std::string tier)
{
    return AssetMetric{std::move(label), std::move(value), std::move(unit), std::move(tier)};
}

std::vector<DhunAsset> buildAssets(const PhaseFlows& flows)
{
    const PhaseConfig& config = *flows.config;
    const double carbonKgDay = flows.carbonKgDay;
    const double carbonFactor = flows.carbonFactor;
    const double sequestrationKgDay = flows.sequestrationKgDay;

    auto carbonOf = [&](double kwh) { return kwh * carbonFactor; };
    auto pct = [&](double value) { return carbonKgDay > 0 ? value / carbonKgDay : 0; };

    const double residenceKwh = flows.buildingKwhDay * 0.58;
    const double schoolKwh = flows.buildingKwhDay * 0.22;
    const double campusKwh = flows.buildingKwhDay * 0.2;

    const double residenceCarbon = carbonOf(residenceKwh);
    const double schoolCarbon = carbonOf(schoolKwh);
    const double campusCarbon = carbonOf(campusKwh);
    const double computeCarbon = carbonOf(flows.computeKwhDay);
    const double stpCarbon = carbonOf(flows.stpKwhDay);
    const double pumpCarbon = carbonOf(flows.pumpKwhDay);
    const double wasteCarbon = carbonOf(flows.wasteKwhDay);

    const double gPerMillionTokens = (flows.computeKwhDay / 24) * 1000 * carbonFactor;

    const double evShare = 0.05;

    std::vector<DhunAsset> assets;

    {
        DhunAsset a;
        a.id = "bioreserve";
        a.name = "Bioreserve Core";
        a.shortName = "Bioreserve";
        a.zone = "bioreserve";
        a.icon = "trees";
        a.status = "healthy";
        a.x = 150;
        a.y = 125;
        a.carbonKgDay = -(sequestrationKgDay * 0.833);
        a.carbonShare = -(sequestrationKgDay * 0.833) / std::max(1.0, carbonKgDay);
        a.headlineValue = round(STANDING_STOCK_T / 1000) + "k t";
        a.headlineLabel = "standing carbon stock";
        a.metrics = {
            m("Protected area", "400", "acres", "MEASURED"),
            m("Trees", "300,000+", "", "MEASURED"),
            m("Bird species", "180+", "", "MEASURED"),
            m("Land cooling", "−8", "°C", "REPORT"),
            m("Sequestration", "1,500", "tCO₂e/yr", "ASSUMPTION"),
        };
        a.note = "Existing stock, not an offset. The twin keeps protected biomass separate from additional sequestration so the two are never double-counted.";
        a.evidence = "MEASURED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "food-farms";
        a.name = "Food Farms & Miyawaki";
        a.shortName = "Food farms";
        a.zone = "bioreserve";
        a.icon = "wheat";
        a.status = "healthy";
        a.x = 300;
        a.y = 82;
        a.carbonKgDay = -(sequestrationKgDay * 0.167);
        a.carbonShare = -(sequestrationKgDay * 0.167) / std::max(1.0, carbonKgDay);
        a.headlineValue = "48,000 m²";
        a.headlineLabel = "Miyawaki plantation";
        a.metrics = {
            m("Food farms", "50,000", "m²", "REPORT"),
            m("Miyawaki", "48,000", "m²", "REPORT"),
            m("Sink", "300", "tCO₂e/yr", "ASSUMPTION"),
            m("Irrigation", "1.16", "L/m²/day", "REPORT"),
        };
        a.note = "Faster establishment than conventional plantation. Sequestration is modelled on a growth curve, not a flat annual rate.";
        a.evidence = "REPORT";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "water-bodies";
        a.name = "Water Bodies & Recharge";
        a.shortName = "Water bodies";
        a.zone = "water";
        a.icon = "waves";
        a.status = "healthy";
        a.x = 378;
        a.y = 300;
        a.waterKlDay = config.harvestKlYr / 365;
        a.headlineValue = round(config.harvestKlYr / 1000) + "k kL";
        a.headlineLabel = "harvested per year";
        a.metrics = {
            m("Harvested to date", "600M", "litres", "MEASURED"),
            m("Storage potential", "275,800", "m³", "REPORT"),
            m("Rainfall", "375–450", "mm/yr", "REPORT"),
            m("Groundwater", "Zero", "extraction", "REPORT"),
        };
        a.note = "Storage behaves like a seasonal battery. The twin sizes it against monsoon inflow and dry-season demand.";
        a.evidence = "MEASURED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "solar";
        a.name = "Solar Farm";
        a.shortName = "Solar";
        a.zone = "energy";
        a.icon = "sun";
        a.status = "healthy";
        a.x = 545;
        a.y = 100;
        a.powerKw = flows.solarKw;
        a.energyKwhDay = flows.solarKwhDay;
        a.headlineValue = round(flows.solarKw) + " kW";
        a.headlineLabel = "generating now";
        a.metrics = {
            m("Installed", round(config.solarKwp), "kWp", "REPORT"),
            m("Specific yield", "1,547", "kWh/kWp/yr", "REPORT"),
            m("Soiling loss", "3", "%", "ASSUMPTION"),
            m("Renewable share", round(flows.renewableShare * 100) + "%", "", "SIMULATED"),
        };
        a.note = "Roof and carport mounted to stay below the tree-height covenant. Dust and cleaning are modelled explicitly.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "grid-hub";
        a.name = "Energy Hub";
        a.shortName = "Grid hub";
        a.zone = "energy";
        a.icon = "zap";
        a.status = "healthy";
        a.x = 655;
        a.y = 62;
        a.powerKw = flows.gridKw;
        a.energyKwhDay = flows.gridKwhDay;
        a.headlineValue = round(flows.gridKw) + " kW";
        a.headlineLabel = "grid import now";
        a.metrics = {
            m("Incoming line", "11", "kV", "REPORT"),
            m("Transformer bank", "300", "kVA", "ASSUMPTION"),
            m("Power factor", "0.97", "", "ASSUMPTION"),
            m("Grid factor", "0.85", "kg/kWh", "REPORT"),
        };
        a.note = "Supplies 100% of residual load. Transformer ratings are unverified field notes and must be reconciled with the single-line diagram.";
        a.evidence = "ASSUMPTION";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "battery";
        a.name = "Battery Storage";
        a.shortName = "Battery";
        a.zone = "energy";
        a.icon = "battery";
        a.status = "healthy";
        a.x = 728;
        a.y = 150;
        a.powerKw = flows.solarKw > flows.demandKw
                        ? -(flows.solarKw - flows.demandKw) * 0.55
                        : std::min(flows.demandKw * 0.12, 20.0);
        a.headlineValue = round(flows.batterySoc * 100) + "%";
        a.headlineLabel = "state of charge";
        a.metrics = {
            m("Usable capacity", round(flows.demandKwhDay * 0.12), "kWh", "ASSUMPTION"),
            m("Round-trip", "92", "%", "ASSUMPTION"),
            m("Chemistry", "LFP", "", "ASSUMPTION"),
            m("Autonomy", "4", "hours", "ASSUMPTION"),
        };
        a.note = "Sized on critical-load hours, not on a percentage of PV capacity. The 25%-of-PV rule in the field notes mixes power and energy units.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "stp";
        a.name = "STP & Water Reuse";
        a.shortName = "STP";
        a.zone = "water";
        a.icon = "droplets";
        a.status = "healthy";
        a.x = 404;
        a.y = 354;
        a.powerKw = flows.stpKw;
        a.energyKwhDay = flows.stpKwhDay;
        a.carbonKgDay = stpCarbon;
        a.waterKlDay = config.stpKlDay;
        a.carbonShare = pct(stpCarbon);
        a.headlineValue = round(config.stpKlDay) + " kL";
        a.headlineLabel = "treated per day";
        a.metrics = {
            m("Collection", "100", "%", "REPORT"),
            m("Reuse", "100", "%", "REPORT"),
            m("Energy", "0.5", "kWh/kL", "ASSUMPTION"),
            m("Standard", "Tertiary", "", "REPORT"),
        };
        a.note = "Treated water goes to flushing and landscape. The twin treats the STP as an energy load, not just a water asset.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "pumping";
        a.name = "Pumping & Distribution";
        a.shortName = "Pumping";
        a.zone = "water";
        a.icon = "gauge";
        a.status = pumpCarbon / std::max(1.0, carbonKgDay) > 0.12 ? "watch" : "healthy";
        a.x = 322;
        a.y = 344;
        a.powerKw = flows.pumpKw;
        a.energyKwhDay = flows.pumpKwhDay;
        a.carbonKgDay = pumpCarbon;
        a.waterKlDay = (config.population * LITRES_PER_PERSON_DAY) / 1000;
        a.carbonShare = pct(pumpCarbon);
        a.headlineValue = round((config.population * LITRES_PER_PERSON_DAY) / 1000) + " kL";
        a.headlineLabel = "demand per day";
        a.metrics = {
            m("Per capita", "78", "L/day", "REPORT"),
            m("Motor class", "IE5", "target", "REPORT"),
            m("Specific energy", "0.4", "kWh/kL", "ASSUMPTION"),
            m("Non-revenue water", "8", "%", "ASSUMPTION"),
        };
        a.note = "The hidden Scope 2 load. Pumping energy is small in kWh but continuous, and it is usually left out of carbon reporting.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "residences";
        a.name = "R1 Residences";
        a.shortName = "Residences";
        a.zone = "built";
        a.icon = "home";
        a.status = assetStatus(pct(residenceCarbon));
        a.x = 360;
        a.y = 408;
        a.powerKw = flows.buildingKw * 0.58;
        a.energyKwhDay = residenceKwh;
        a.carbonKgDay = residenceCarbon;
        a.carbonShare = pct(residenceCarbon);
        a.headlineValue = js::toString(config.epi);
        a.headlineLabel = "kWh/m²/yr EPI";
        a.metrics = {
            m("Efficient case", "47.1", "kWh/m²/yr", "REPORT"),
            m("Baseline", "134", "kWh/m²/yr", "REPORT"),
            m("Envelope", "12", "W/m² RETV", "REPORT"),
            m("Construction ceiling", "Tree height", "", "MEASURED"),
        };
        a.note = "The EcoNiwas efficient case is 65% lower than business-as-usual. The twin tracks the gap between design intent and operation.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "school";
        a.name = "School Campus";
        a.shortName = "School";
        a.zone = "built";
        a.icon = "school";
        a.status = assetStatus(pct(schoolCarbon));
        a.x = 486;
        a.y = 418;
        a.powerKw = flows.buildingKw * 0.22;
        a.energyKwhDay = schoolKwh;
        a.carbonKgDay = schoolCarbon;
        a.carbonShare = pct(schoolCarbon);
        a.headlineValue = "300";
        a.headlineLabel = "students";
        a.metrics = {
            m("Site", "7", "acres", "REPORT"),
            m("Students", "300", "", "REPORT"),
            m("Peak ambient", "47–50", "°C", "MEASURED"),
            m("Cooling", "Passive first", "", "ASSUMPTION"),
        };
        a.note = "Passive cooling, shading and efficient standalone AC before mechanical load. The school is the Phase 1 testbed for the envelope strategy.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "campus-centre";
        a.name = "Campus Centre & Hospitality";
        a.shortName = "Hospitality";
        a.zone = "built";
        a.icon = "building";
        a.status = assetStatus(pct(campusCarbon));
        a.x = 552;
        a.y = 348;
        a.powerKw = flows.buildingKw * 0.2;
        a.energyKwhDay = campusKwh;
        a.carbonKgDay = campusCarbon;
        a.carbonShare = pct(campusCarbon);
        a.headlineValue = "2029";
        a.headlineLabel = "hospitality target";
        a.metrics = {
            m("Programme", "Wellness", "", "REPORT"),
            m("Shared facilities", "Kitchen, events", "", "REPORT"),
            m("EV parking", round(evShare * 100) + "%", "", "REPORT"),
            m("Lighting", "Full cutoff", "", "ASSUMPTION"),
        };
        a.note = "Shared amenities concentrate load and create the campus's evening peak. This is where demand flexibility matters most.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "gaushala";
        a.name = "Gaushala & Compost";
        a.shortName = "Gaushala";
        a.zone = "waste";
        a.icon = "recycle";
        a.status = "healthy";
        a.x = 145;
        a.y = 388;
        a.powerKw = flows.wasteKw;
        a.energyKwhDay = flows.wasteKwhDay;
        a.carbonKgDay = wasteCarbon;
        a.carbonShare = pct(wasteCarbon);
        a.headlineValue = js::toString(config.organicTDay);
        a.headlineLabel = "t organic / day";
        a.metrics = {
            m("Herd", "150–200", "cattle", "ASSUMPTION"),
            m("Organic diverted", "100", "%", "REPORT"),
            m("Methane avoided", round(config.organicTDay * 700), "kgCO₂e/day", "ASSUMPTION"),
            m("Compost to farms", "Closed loop", "", "ASSUMPTION"),
        };
        a.note = "Aerobic composting avoids the methane a landfill would produce. The avoided emissions are shown, not silently netted off.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    {
        DhunAsset a;
        a.id = "compute";
        a.name = "Zugrama Compute Commons";
        a.shortName = "Compute";
        a.zone = "compute";
        a.icon = "cpu";
        a.status = config.computePue > 1.6 ? "watch" : "healthy";
        a.x = 672;
        a.y = 312;
        a.powerKw = flows.computeKw;
        a.energyKwhDay = flows.computeKwhDay;
        a.carbonKgDay = computeCarbon;
        a.waterKlDay = flows.computeKwhDay * 1.8;
        a.carbonShare = pct(computeCarbon);
        a.headlineValue = round(config.computeKw) + " kW";
        a.headlineLabel = "IT load";
        a.metrics = {
            m("PUE", js::toFixed(config.computePue, 2), "", "ASSUMPTION"),
            m("WUE", "1.8", "L/kWh", "ASSUMPTION"),
            m("Carbon", round(gPerMillionTokens, 0), "gCO₂e / 1M tokens", "SIMULATED"),
            m("Flexibility", "Deferrable", "", "ASSUMPTION"),
        };
        a.note = "The only load on site that can move in time. The twin schedules inference into high-solar, low-carbon hours instead of running it flat out.";
        a.evidence = "SIMULATED";
        assets.push_back(std::move(a));
    }

    return assets;
}

}  // namespace

double clamp(double value, double min, double max)
{
    return std::min(std::max(value, min), max);
}

/* Solar shape: sunrise 06:15, sunset 18:20 */
double solarShape(double fraction)
{
    const double start = 0.26;
    const double end = 0.765;

    if (fraction <= start || fraction >= end) return 0;

    const double x = (fraction - start) / (end - start);
    return std::pow(std::sin(PI * x), 1.15);
}

/* Marginal grid carbon intensity — planning assumption. */
double gridIntensity(double fraction)
{
    const double evening = std::exp(-std::pow((fraction - 0.79) / 0.075, 2));
    const double midday = std::exp(-std::pow((fraction - 0.53) / 0.11, 2));
    return 0.86 + 0.14 * evening - 0.2 * midday;
}

TwinSnapshot buildTwin(PhaseId phase, const SiteTime& now)
{
    const PhaseFlows flows = phaseFlows(phase, now);
    TwinSnapshot snapshot;
    snapshot.assets = buildAssets(flows);

    const PhaseConfig& config = *flows.config;
    const double netCarbonKgDay = flows.carbonKgDay - flows.sequestrationKgDay;

    TwinSummary& s = snapshot.summary;
    s.phase = phase;
    s.clock = now;
    s.population = config.population;
    s.builtM2 = config.builtM2;
    s.demandKw = flows.demandKw;
    s.solarKw = flows.solarKw;
    s.gridKw = flows.gridKw;
    s.batterySoc = flows.batterySoc;

    s.workstreamCampusKw = flows.workstreamCampusKw;
    s.workstreamCampusKwhDay = flows.workstreamCampusKwhDay;
    s.workstreamComputeKw = flows.workstreamComputeKw;
    s.workstreamComputeKwhDay = flows.workstreamComputeKwhDay;
    s.workstreamSpirulinaKw = flows.workstreamSpirulinaKw;
    s.workstreamSpirulinaKwhDay = flows.workstreamSpirulinaKwhDay;

    s.renewableShare = flows.renewableShare;
    s.gridIntensity = gridIntensity(dayFraction(now));
    s.demandKwhDay = flows.demandKwhDay;
    s.solarKwhDay = flows.solarKwhDay;
    s.gridKwhDay = flows.gridKwhDay;
    s.carbonKgDay = flows.carbonKgDay;
    s.sequestrationKgDay = flows.sequestrationKgDay;
    s.netCarbonKgDay = netCarbonKgDay;
    s.waterKlDay = (config.population * LITRES_PER_PERSON_DAY) / 1000;
    s.waterHarvestKlYr = config.harvestKlYr;
    s.standingStockT = STANDING_STOCK_T;
    s.perCapitaT = config.population > 0
                       ? (netCarbonKgDay * 365) / 1000 / config.population
                       : 0;
    s.targetPerCapitaT = TARGET_PER_CAPITA_T;

    return snapshot;
}

double landscapeWaterKlYr(PhaseId phase)
{
    const PhaseConfig& config = phaseConfig(phase);
    return LANDSCAPE_KL_YR_BUILD_OUT * clamp(config.builtM2 / BUILD_OUT_M2);
}

AnnualEnergy annualEnergy(PhaseId phase)
{
    const PhaseConfig& config = phaseConfig(phase);
    AnnualEnergy e;

    e.buildingKwhYr = config.builtM2 * config.epi;

    e.computeKwhYr = config.computeKw * config.computePue * 24 * 365;

    e.waterKwhYr = (config.stpKlDay * STP_ENERGY_KWH_PER_KL +
                    ((config.population * LITRES_PER_PERSON_DAY) / 1000) * PUMP_ENERGY_KWH_PER_KL) *
                   365;

    e.wasteKwhYr = (0.8 + config.organicTDay * 2) * 24 * 365;

    e.spirulinaKwhYr = config.spirulinaKw * 24 * 365;

    e.campusKwhYr = e.buildingKwhYr + e.waterKwhYr + e.wasteKwhYr;

    e.demandKwhYr = e.campusKwhYr + e.computeKwhYr + e.spirulinaKwhYr;

    e.solarKwhYr = config.solarKwp * SPECIFIC_YIELD * SOILING_FACTOR;

    const double usableSolarKwhYr = std::min(e.solarKwhYr, e.demandKwhYr) * SELF_CONSUMPTION_FACTOR;

    e.gridKwhYr = std::max(0.0, e.demandKwhYr - usableSolarKwhYr);

    e.renewableShare =
        e.demandKwhYr > 0 ? std::min(1.0, usableSolarKwhYr / e.demandKwhYr) : 0;
    e.carbonT = (e.gridKwhYr * GRID_FACTOR) / 1000;
    e.sinkT = TOTAL_SEQUESTRATION_T_YR;
    return e;
}

/* ---------- JSON, in upstream field order ---------- */

Json toJson(const AssetMetric& metric)
{
    Json j;
    j["label"] = metric.label;
    j["value"] = metric.value;
    if (!metric.unit.empty()) j["unit"] = metric.unit;
    j["tier"] = metric.tier;
    return j;
}

Json toJson(const DhunAsset& a)
{
    Json j;
    j["id"] = a.id;
    j["name"] = a.name;
    j["short"] = a.shortName;
    j["zone"] = a.zone;
    j["icon"] = a.icon;
    j["status"] = a.status;
    j["x"] = a.x;
    j["y"] = a.y;
    j["powerKw"] = a.powerKw;
    j["energyKwhDay"] = a.energyKwhDay;
    j["carbonKgDay"] = a.carbonKgDay;
    j["waterKlDay"] = a.waterKlDay;
    j["carbonShare"] = a.carbonShare;
    j["headlineValue"] = a.headlineValue;
    j["headlineLabel"] = a.headlineLabel;
    Json metrics = Json::array();
    for (const auto& metric : a.metrics) metrics.push_back(toJson(metric));
    j["metrics"] = metrics;
    j["note"] = a.note;
    j["evidence"] = a.evidence;
    return j;
}

Json toJson(const TwinSummary& s)
{
    Json j;
    j["phase"] = std::string(phaseKey(s.phase));
    j["clock"] = siteIso(s.clock);
    j["population"] = s.population;
    j["builtM2"] = s.builtM2;
    j["demandKw"] = s.demandKw;
    j["solarKw"] = s.solarKw;
    j["gridKw"] = s.gridKw;
    j["batterySoc"] = s.batterySoc;
    j["workstreamCampusKw"] = s.workstreamCampusKw;
    j["workstreamCampusKwhDay"] = s.workstreamCampusKwhDay;
    j["workstreamComputeKw"] = s.workstreamComputeKw;
    j["workstreamComputeKwhDay"] = s.workstreamComputeKwhDay;
    j["workstreamSpirulinaKw"] = s.workstreamSpirulinaKw;
    j["workstreamSpirulinaKwhDay"] = s.workstreamSpirulinaKwhDay;
    j["renewableShare"] = s.renewableShare;
    j["gridIntensity"] = s.gridIntensity;
    j["demandKwhDay"] = s.demandKwhDay;
    j["solarKwhDay"] = s.solarKwhDay;
    j["gridKwhDay"] = s.gridKwhDay;
    j["carbonKgDay"] = s.carbonKgDay;
    j["sequestrationKgDay"] = s.sequestrationKgDay;
    j["netCarbonKgDay"] = s.netCarbonKgDay;
    j["waterKlDay"] = s.waterKlDay;
    j["waterHarvestKlYr"] = s.waterHarvestKlYr;
    j["standingStockT"] = s.standingStockT;
    j["perCapitaT"] = s.perCapitaT;
    j["targetPerCapitaT"] = s.targetPerCapitaT;
    return j;
}

Json toJson(const TwinSnapshot& snapshot)
{
    Json j;
    j["summary"] = toJson(snapshot.summary);
    Json assets = Json::array();
    for (const auto& asset : snapshot.assets) assets.push_back(toJson(asset));
    j["assets"] = assets;
    return j;
}

Json toJson(const AnnualEnergy& e)
{
    Json j;
    j["demandKwhYr"] = e.demandKwhYr;
    j["campusKwhYr"] = e.campusKwhYr;
    j["computeKwhYr"] = e.computeKwhYr;
    j["spirulinaKwhYr"] = e.spirulinaKwhYr;
    j["buildingKwhYr"] = e.buildingKwhYr;
    j["waterKwhYr"] = e.waterKwhYr;
    j["wasteKwhYr"] = e.wasteKwhYr;
    j["solarKwhYr"] = e.solarKwhYr;
    j["gridKwhYr"] = e.gridKwhYr;
    j["renewableShare"] = e.renewableShare;
    j["carbonT"] = e.carbonT;
    j["sinkT"] = e.sinkT;
    return j;
}

}  // namespace dhun
