#include "parity_cases.h"

#include "dhun/commons.h"
#include "dhun/planning.h"
#include "dhun/simulation.h"

namespace dhun::test {

namespace {

PhaseId phaseOf(const Json& value)
{
    return *parsePhase(value.get<std::string>());
}

void simulationCase(const Json& golden, Report& r)
{
    for (const auto& item : golden["twins"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        const double ms = item["ms"].get<double>();
        const auto twin = buildTwin(phase, SiteTime::fromEpochMs(ms));
        compare(item["twin"], toJson(twin),
                "twin[" + item["phase"].get<std::string>() + "@" + js::toString(ms) + "]", r);
    }

    for (const auto& item : golden["annual"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        compare(item["energy"], toJson(annualEnergy(phase)), "annual." + item["phase"].get<std::string>(), r);
        compare(item["landscape"], Json(landscapeWaterKlYr(phase)), "landscape", r);
    }

    for (const auto& item : golden["shapes"]) {
        const double f = item["f"].get<double>();
        compare(item["solar"], Json(solarShape(f)), "solarShape", r);
        compare(item["grid"], Json(gridIntensity(f)), "gridIntensity", r);
    }
}

void planningCase(const Json& golden, Report& r)
{
    for (const auto& item : golden["ledger"]) {
        compare(item["ledger"], buildLedger(phaseOf(item["phase"]), item["options"]),
                "ledger." + item["phase"].get<std::string>() + js::stringify(item["options"]), r);
    }

    const auto& opts = golden["scenarioOptions"];
    compare(opts["COMPUTE_LOAD_KW"], computeLoadKw(), "COMPUTE_LOAD_KW", r);
    compare(opts["COMPUTE_LABEL"], computeLabels(), "COMPUTE_LABEL", r);
    compare(opts["COOLING_PROFILE"], coolingProfiles(), "COOLING_PROFILE", r);
    compare(opts["COOLING_LABEL"], coolingLabels(), "COOLING_LABEL", r);

    for (const auto& item : golden["defaults"]) {
        compare(item["inputs"], defaultScenario(phaseOf(item["phase"])), "defaultScenario", r);
    }
    for (const auto& item : golden["scenarios"]) {
        compare(item["result"], runScenario(phaseOf(item["phase"]), item["inputs"]),
                "runScenario." + item["phase"].get<std::string>() + js::stringify(item["inputs"]), r);
    }
    for (const auto& item : golden["intensity"]) {
        compare(item["value"], Json(marginalIntensity(phaseOf(item["phase"]), item["hour"].get<double>())),
                "marginalIntensity", r);
    }
    for (const auto& item : golden["windows"]) {
        compare(item["windows"], computeWindows(phaseOf(item["phase"])), "computeWindows", r);
    }
    compare(golden["queue"], computeQueue(), "computeQueue", r);
    for (const auto& item : golden["jobs"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        compare(item["energy"], Json(jobEnergyKwh(phase, item["job"])), "jobEnergyKwh", r);
        compare(item["carbon"], jobCarbon(phase, item["job"], item["hour"].get<double>()),
                "jobCarbon@" + js::toString(item["hour"].get<double>()), r);
    }
    for (const auto& item : golden["zones"]) {
        const auto twin = buildTwin(phaseOf(item["phase"]), SiteTime::fromEpochMs(item["ms"].get<double>()));
        compare(item["pressures"], toJson(zonePressures(twin.summary, twin.assets)), "zonePressures", r);
        compare(item["issues"], zoneIssues(twin.summary, twin.assets), "zoneIssues", r);
    }
}

std::optional<double> optArg(const Json& args, std::size_t i)
{
    if (args.size() <= i || !args[i].is_number()) return std::nullopt;
    return args[i].get<double>();
}

void commonsCase(const Json& golden, Report& r)
{
    const auto& d = golden["defaults"];
    compare(d["COMPUTE_DEFAULT_SCENARIOS"], computeDefaultScenarios(), "COMPUTE_DEFAULT_SCENARIOS", r);
    compare(d["CAMPUS_ENERGY_DEFAULTS"], campusEnergyDefaults(), "CAMPUS_ENERGY_DEFAULTS", r);
    compare(d["DEFAULT_WORKLOAD_PROFILES"], defaultWorkloadProfiles(), "DEFAULT_WORKLOAD_PROFILES", r);
    compare(d["COMPUTE_EVIDENCE_REGISTER"], computeEvidenceRegister(), "COMPUTE_EVIDENCE_REGISTER", r);

    for (const auto& item : golden["computed"]) {
        const Json outputs = calculateComputeScenario(item["inputs"]);
        compare(item["outputs"], outputs, "calculateComputeScenario" + js::stringify(item["inputs"]), r);
        compare(item["solar"], calculateSolarBatteryInteraction(item["inputs"], outputs),
                "calculateSolarBatteryInteraction", r);
    }
    for (const auto& item : golden["impacts"]) {
        const auto& a = item["args"];
        compare(item["impact"],
                calculateCampusEnergyImpact(a[0].get<double>(), a[1].get<bool>(), a[2].get<bool>(),
                                            optArg(a, 3), optArg(a, 4)),
                "calculateCampusEnergyImpact" + js::stringify(a), r);
    }
    for (const auto& item : golden["aggregate"]) {
        compare(item["aggregate"], calculateAggregateBills(item["bills"]), "calculateAggregateBills", r);
    }
    for (const auto& item : golden["reconcile"]) {
        const auto& a = item["args"];
        const double spend = a.size() > 0 ? a[0].get<double>() : 250000;
        const double tariff = a.size() > 1 ? a[1].get<double>() : 10.0;
        const Json* bills = a.size() > 2 ? &a[2] : nullptr;
        compare(item["result"], reconcileBaselineWithEvidence(spend, tariff, bills),
                "reconcileBaselineWithEvidence" + js::stringify(a).substr(0, 40), r);
    }
    for (const auto& item : golden["stacked"]) {
        const auto& a = item["args"];
        compare(item["result"],
                calculateStackedCampusDemand(a[0].get<double>(), a[1].get<double>(), a[2].get<bool>(),
                                             a[3].get<double>(), a[4].get<std::string>(),
                                             a.size() > 5 ? a[5].get<double>() : 10.0,
                                             a.size() > 6 ? a[6].get<double>() : 0.71),
                "calculateStackedCampusDemand", r);
    }
    for (const auto& item : golden["masks"]) {
        compare(item["masked"], Json(maskAccountNumber(item["acc"].get<std::string>())), "maskAccountNumber", r);
    }
    const auto& v = golden["validation"];
    for (const auto& item : v["energy"]) compare(item["result"], validateEnergyBalance(item["params"]), "validateEnergyBalance", r);
    for (const auto& item : v["water"]) compare(item["result"], validateWaterBalance(item["params"]), "validateWaterBalance", r);
    for (const auto& item : v["carbon"]) compare(item["result"], validateCarbonBoundaries(item["params"]), "validateCarbonBoundaries", r);
    for (const auto& item : v["battery"]) compare(item["result"], validateBatteryDispatch(item["params"]), "validateBatteryDispatch", r);
    for (const auto& item : v["compute"]) compare(item["result"], validateComputeMonitoring(item["params"]), "validateComputeMonitoring", r);
}

}  // namespace

const std::map<std::string, ParityCase>& parityCases()
{
    static const std::map<std::string, ParityCase> cases = {
        {"simulation", simulationCase},
        {"planning", planningCase},
        {"commons", commonsCase},
    };
    return cases;
}

}  // namespace dhun::test
