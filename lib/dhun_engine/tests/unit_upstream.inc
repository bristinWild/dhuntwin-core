/* ============================================================
   UPSTREAM TEST SUITES, PORTED
   The DhunTwin OS repository ships three verification suites:
     src/features/dhun/engine/__tests__/computeCommons.test.ts
     src/features/dhun/energy/__tests__/energyEvidence.test.ts
     src/features/dhun/registry/__tests__/assetRegistry.test.ts
   Every test and assertion of those files is reproduced here
   against dhun_engine, in the same order and with the same names.
   ============================================================ */

#include <set>

#include "dhun/commons.h"
#include "dhun/gis.h"
#include "dhun/jsonutil.h"
#include "dhun/registry.h"

namespace upstream {

int asserts = 0;

}  // namespace upstream

#define UP(cond, message)                                                              \
    do {                                                                               \
        ++upstream::asserts;                                                           \
        if (!(cond)) dhun::test::fail(std::string("assert: ") + (message), __FILE__, __LINE__); \
    } while (0)

namespace {

using dhun::Json;

double d(const Json& j) { return j.get<double>(); }

Json scenario(const char* key) { return dhun::computeDefaultScenarios()[key]; }

Json with(Json base, const char* key, const Json& value)
{
    base[key] = value;
    return base;
}

const Json& billFor(const std::string& propertyId)
{
    for (const auto& b : dhun::historicalReferenceBills()) {
        if (b["propertyId"] == propertyId) return b;
    }
    static const Json none = nullptr;
    return none;
}

bool anyIncludes(const Json& list, const std::string& needle)
{
    for (const auto& item : list) {
        if (item.get<std::string>().find(needle) != std::string::npos) return true;
    }
    return false;
}

/* a repository on a fresh localStorage, as each upstream test builds */
dhun::AssetRepository freshRepo()
{
    dhun::AssetRepository repo(dhun::stringStorage());
    repo.resetToDefaults();
    return repo;
}

}  // namespace

/* ---------------- computeCommons.test.ts ---------------- */

TEST("upstream computeCommons: Scenario A - Compute Pilot")
{
    const Json out = dhun::calculateComputeScenario(scenario("pilot"));
    UP(d(out["annualOperatingHours"]) == 4380, "Pilot: annual operating hours equals 4,380 h (12h × 365d)");
    UP(d(out["annualItEnergyKwh"]) == 21900, "Pilot: annual IT energy equals 21,900 kWh (5 kW × 4,380 h)");
    UP(std::fabs(d(out["annualFacilityEnergyKwh"]) - 28470) < 0.01, "Pilot: annual facility energy equals 28,470 kWh (~28.5 MWh) at PUE 1.30");
    UP(std::fabs(d(out["annualFacilityEnergyMwh"]) - 28.47) < 0.01, "Pilot: annual facility energy in MWh equals 28.47 MWh");
    UP(std::fabs(d(out["annualFacilityOverheadKwh"]) - 6570) < 0.01, "Pilot: facility overhead equals 6,570 kWh (28,470 - 21,900)");
    UP(d(out["avgFacilityDemandKw"]) == 6.5, "Pilot: average facility demand equals 6.5 kW (5 kW × 1.30)");
    UP(d(out["annualElectricityCostInr"]) == 284700, "Pilot: annual electricity cost equals ₹2,84,700 (~₹2.85 Lakh)");
    UP(std::fabs(d(out["annualElectricityCostLakh"]) - 2.847) < 0.001, "Pilot: annual cost in Lakh equals ₹2.847 Lakh");
    UP(std::fabs(d(out["monthlyFacilityEnergyKwh"]) - 2372.5) < 0.01, "Pilot: monthly facility energy equals 2,372.5 kWh (28,470 / 12)");
    UP(std::fabs(d(out["monthlyElectricityCostInr"]) - 23725) < 0.01, "Pilot: monthly electricity cost equals ₹23,725 (284,700 / 12)");
}

TEST("upstream computeCommons: Scenario B - Phase 1 Reference")
{
    const Json out = dhun::calculateComputeScenario(scenario("phase1"));
    UP(d(out["annualOperatingHours"]) == 8760, "Phase 1: annual operating hours equals 8,760 h (24h × 365d)");
    UP(d(out["annualItEnergyKwh"]) == 175200, "Phase 1: annual IT energy equals 175,200 kWh (20 kW × 8,760 h)");
    UP(std::fabs(d(out["annualFacilityEnergyKwh"]) - 227760) < 0.01, "Phase 1: annual facility energy equals 227,760 kWh (~227.8 MWh)");
    UP(std::fabs(d(out["annualFacilityEnergyMwh"]) - 227.76) < 0.01, "Phase 1: annual facility energy in MWh equals 227.76 MWh");
    UP(std::fabs(d(out["annualFacilityOverheadKwh"]) - 52560) < 0.01, "Phase 1: annual facility overhead equals 52,560 kWh");
    UP(d(out["avgFacilityDemandKw"]) == 26.0, "Phase 1: average facility demand equals 26.0 kW (20 kW × 1.30)");
    UP(d(out["annualElectricityCostInr"]) == 2277600, "Phase 1: annual electricity cost equals ₹22,77,600 (~₹22.8 Lakh)");
    UP(std::fabs(d(out["annualElectricityCostLakh"]) - 22.776) < 0.001, "Phase 1: annual cost in Lakh equals ₹22.776 Lakh");
    UP(std::fabs(d(out["monthlyFacilityEnergyKwh"]) - 18980) < 0.01, "Phase 1: monthly facility energy equals 18,980 kWh (227,760 / 12)");
    UP(std::fabs(d(out["monthlyElectricityCostInr"]) - 189800) < 0.01, "Phase 1: monthly electricity cost equals ₹1,89,800 (2,277,600 / 12)");
}

TEST("upstream computeCommons: Scenario C - Expansion Scenario")
{
    const Json out = dhun::calculateComputeScenario(scenario("expansion"));
    UP(d(out["annualOperatingHours"]) == 8760, "Expansion: annual operating hours equals 8,760 h (24h × 365d)");
    UP(d(out["annualItEnergyKwh"]) == 438000, "Expansion: annual IT energy equals 438,000 kWh (50 kW × 8,760 h)");
    UP(std::fabs(d(out["annualFacilityEnergyKwh"]) - 569400) < 0.01, "Expansion: annual facility energy equals 569,400 kWh (~569.4 MWh)");
    UP(std::fabs(d(out["annualFacilityEnergyMwh"]) - 569.4) < 0.01, "Expansion: annual facility energy in MWh equals 569.4 MWh");
    UP(std::fabs(d(out["annualFacilityOverheadKwh"]) - 131400) < 0.01, "Expansion: annual facility overhead equals 131,400 kWh");
    UP(d(out["avgFacilityDemandKw"]) == 65.0, "Expansion: average facility demand equals 65.0 kW (50 kW × 1.30)");
    UP(d(out["annualElectricityCostInr"]) == 5694000, "Expansion: annual electricity cost equals ₹56,94,000 (~₹56.9 Lakh)");
    UP(std::fabs(d(out["annualElectricityCostLakh"]) - 56.94) < 0.001, "Expansion: annual cost in Lakh equals ₹56.94 Lakh");
    UP(std::fabs(d(out["monthlyFacilityEnergyKwh"]) - 47450) < 0.01, "Expansion: monthly facility energy equals 47,450 kWh (569,400 / 12)");
    UP(std::fabs(d(out["monthlyElectricityCostInr"]) - 474500) < 0.01, "Expansion: monthly electricity cost equals ₹4,74,500 (5,694,000 / 12)");
}

TEST("upstream computeCommons: Rated IT Capacity vs Average IT Load & Utilisation")
{
    const Json inputs = Json::parse(R"({
        "id": "test-util", "name": "Rated Utilisation Test", "description": "Testing non-double-counted utilisation",
        "inputMode": "ratedUtilisation", "averageItLoadKw": 999, "ratedItCapacityKw": 40.0,
        "averageUtilisationPct": 50.0, "operatingHoursPerDay": 24, "operatingDaysPerYear": 365,
        "pue": 1.30, "electricityRateInrPerKwh": 10.0 })");
    const Json out = dhun::calculateComputeScenario(inputs);
    UP(d(out["effectiveItLoadKw"]) == 20.0, "Utilisation: correctly derives 20 kW from 40 kW @ 50% utilisation");
    UP(std::fabs(d(out["annualFacilityEnergyMwh"]) - 227.76) < 0.01, "Utilisation: derives exact 227.76 MWh without double-counting utilization");

    const Json direct = dhun::calculateComputeScenario(with(with(inputs, "inputMode", "averageLoad"), "averageItLoadKw", 20.0));
    UP(d(direct["effectiveItLoadKw"]) == 20.0, "Direct Load: correctly uses direct average IT load");
    UP(d(direct["annualFacilityEnergyKwh"]) == d(out["annualFacilityEnergyKwh"]), "Consistency: direct load and rated utilisation produce identical results");
}

TEST("upstream computeCommons: Dynamic Parameter Changes (PUE, Tariff, Hours)")
{
    const Json base = scenario("phase1");
    const Json baseOut = dhun::calculateComputeScenario(base);
    const Json efficient = dhun::calculateComputeScenario(with(base, "pue", 1.15));
    UP(d(efficient["annualItEnergyKwh"]) == 175200, "PUE Sensitivity: IT computing energy is invariant to PUE changes");
    UP(std::fabs(d(efficient["annualFacilityEnergyKwh"]) - 175200 * 1.15) < 0.01, "PUE Sensitivity: facility energy scales linearly with PUE (1.15)");
    UP(d(efficient["annualFacilityOverheadKwh"]) < d(baseOut["annualFacilityOverheadKwh"]), "PUE Sensitivity: lower PUE strictly reduces facility cooling overhead");

    const Json tariff = dhun::calculateComputeScenario(with(base, "electricityRateInrPerKwh", 12.5));
    UP(d(tariff["annualFacilityEnergyKwh"]) == d(baseOut["annualFacilityEnergyKwh"]), "Tariff Sensitivity: tariff change does not alter physical energy consumption");
    UP(d(tariff["annualElectricityCostInr"]) == 227760 * 12.5, "Tariff Sensitivity: cost scales proportionally with new tariff rate");

    const Json hours = dhun::calculateComputeScenario(with(scenario("pilot"), "operatingHoursPerDay", 18));
    UP(d(hours["annualOperatingHours"]) == 18 * 365, "Schedule Sensitivity: annual hours scale with operating hours per day");
    UP(d(hours["annualFacilityEnergyKwh"]) == 5 * 18 * 365 * 1.3, "Schedule Sensitivity: energy scales proportionally with operating schedule");
}

TEST("upstream computeCommons: Campus Energy Integration Totals")
{
    const Json pilot = dhun::calculateComputeScenario(scenario("pilot"));
    const Json p1 = dhun::calculateComputeScenario(scenario("phase1"));
    const Json exp = dhun::calculateComputeScenario(scenario("expansion"));

    UP(std::fabs(d(dhun::calculateCampusEnergyImpact(d(pilot["annualFacilityEnergyMwh"]))["totalModelledCampusMwhYr"]) - 852.47) < 0.1,
       "Campus Integration: Pilot total campus demand equals ~852.5 MWh/yr (300 + 524 + 28.5)");
    UP(std::fabs(d(dhun::calculateCampusEnergyImpact(d(p1["annualFacilityEnergyMwh"]))["totalModelledCampusMwhYr"]) - 1051.76) < 0.1,
       "Campus Integration: Phase 1 total campus demand equals ~1,051.8 MWh/yr (300 + 524 + 227.8)");
    UP(std::fabs(d(dhun::calculateCampusEnergyImpact(d(exp["annualFacilityEnergyMwh"]))["totalModelledCampusMwhYr"]) - 1393.4) < 0.1,
       "Campus Integration: Expansion total campus demand equals ~1,393.4 MWh/yr (300 + 524 + 569.4)");

    const Json without = dhun::calculateCampusEnergyImpact(d(p1["annualFacilityEnergyMwh"]), true, false);
    UP(d(without["plannedBuildingsMwhYr"]) == 0, "Campus Integration: planned buildings can be independently toggled out");
    UP(std::fabs(d(without["totalModelledCampusMwhYr"]) - (300 + 227.76)) < 0.1, "Campus Integration: total recalculates cleanly without planned buildings");
}

TEST("upstream computeCommons: Input Validation & Physical Bounds Enforcement")
{
    const Json invalidPue = dhun::calculateComputeScenario(with(scenario("pilot"), "pue", 0.8));
    UP(!invalidPue["isValid"].get<bool>(), "Validation: flags PUE < 1.00 as physically invalid");
    UP(anyIncludes(invalidPue["validationErrors"], "PUE must be >= 1.00"), "Validation: provides clear thermodynamic rationale for PUE minimum");

    const Json invalidHours = dhun::calculateComputeScenario(with(scenario("pilot"), "operatingHoursPerDay", 28));
    UP(!invalidHours["isValid"].get<bool>(), "Validation: flags operating hours > 24 as invalid");

    const Json negativeTariff = dhun::calculateComputeScenario(with(scenario("pilot"), "electricityRateInrPerKwh", -5));
    UP(!negativeTariff["isValid"].get<bool>(), "Validation: flags negative electricity tariff as invalid");
}

TEST("upstream computeCommons: Solar & Battery Interaction Modeling")
{
    const Json pilot = scenario("pilot");
    const Json pilotSolar = dhun::calculateSolarBatteryInteraction(pilot, dhun::calculateComputeScenario(pilot));
    UP(d(pilotSolar["daytimeCoincidencePct"]) == 75.0, "Solar Interaction: 12h daytime pilot achieves 75% solar coincidence");

    const Json p1 = scenario("phase1");
    const Json p1Solar = dhun::calculateSolarBatteryInteraction(p1, dhun::calculateComputeScenario(p1));
    UP(std::fabs(d(p1Solar["daytimeCoincidencePct"]) - 37.5) < 0.1, "Solar Interaction: 24h continuous operation has ~38% solar coincidence");
    UP(d(p1Solar["nighttimeBatteryCapacityNeededKwh"]) > 0, "Solar Interaction: calculates dedicated BESS buffer needed for night load");
}

TEST("upstream computeCommons: Grid Carbon Calculations")
{
    const Json p1 = dhun::calculateComputeScenario(scenario("phase1"));
    const double expected = (227760 * dhun::GRID_FACTOR) / 1000;
    UP(std::fabs(d(p1["annualCarbonEmissionsTons"]) - expected) < 0.01, "Carbon Accounting: annual emissions use verified Dhun grid factor (0.71 kg/kWh)");
}

/* ---------------- energyEvidence.test.ts ---------------- */

TEST("upstream energyEvidence: Bill 1 (Vastu Farms): accurately records 30 kW connected load and 1,490.4 kWh")
{
    const Json& bill = billFor("prop-vastu-farms");
    UP(!bill.is_null(), "Vastu Farms record must exist");
    UP(d(bill["connectedLoadKw"]) == 30, "Connected load must be 30 kW");
    UP(d(bill["recordedBillingDemandKw"]) == 22.5, "Recorded demand must be 22.5 kW");
    UP(d(bill["consumptionKwh"]) == 1490.4, "Consumption must be 1,490.4 kWh");
    UP(d(bill["currentBillAmount"]) == 21203.49, "Current bill amount must be ₹21,203.49");
    UP(d(bill["previousArrears"]) == 0, "Previous arrears must be ₹0");
    UP(d(bill["powerFactor"]) == 0.923, "Power factor must be 0.923");
}

TEST("upstream energyEvidence: Bill 2 (Vastu Resorts): accurately records 60 kW load, ₹71,919.43 arrears, and ₹61,638.88 current")
{
    const Json& bill = billFor("prop-vastu-resorts");
    UP(!bill.is_null(), "Vastu Resorts record must exist");
    UP(d(bill["connectedLoadKw"]) == 60, "Connected load must be 60 kW");
    UP(d(bill["recordedBillingDemandKw"]) == 45, "Recorded demand must be 45 kW");
    UP(d(bill["consumptionKwh"]) == 4949.1, "Consumption must be 4,949.1 kWh");
    UP(d(bill["currentBillAmount"]) == 61638.88, "Current bill must be ₹61,638.88");
    UP(d(bill["previousArrears"]) == 71919.43, "Previous arrears must be ₹71,919.43");
    UP(d(bill["totalAmountPayable"]) == 136096.0, "Reported total payable must be ₹1,36,096.00");
    UP(d(bill["powerFactor"]) == 0.88, "Power factor must be 0.880");
}

TEST("upstream energyEvidence: Bill 3 (Vastu Abode): accurately records 100 kW load and 4,692.63 kWh consumption")
{
    const Json& bill = billFor("prop-vastu-abode");
    UP(!bill.is_null(), "Vastu Abode record must exist");
    UP(d(bill["connectedLoadKw"]) == 100, "Connected load must be 100 kW");
    UP(d(bill["recordedBillingDemandKw"]) == 56.25, "Recorded demand must be 56.25 kW");
    UP(d(bill["consumptionKwh"]) == 4692.63, "Consumption must be 4,692.63 kWh");
    UP(d(bill["currentBillAmount"]) == 60116.41, "Current bill must be ₹60,116.41");
    UP(d(bill["previousArrears"]) == 0, "Previous arrears must be ₹0");
    UP(d(bill["powerFactor"]) == 0.955, "Power factor must be 0.955");
}

TEST("upstream energyEvidence: Aggregates")
{
    const Json agg = dhun::calculateAggregateBills(dhun::historicalReferenceBills());
    UP(std::fabs(d(agg["combinedMonthlyKwh"]) - 11132.13) < 0.001, "Combined kWh must be 11,132.13");
    UP(std::fabs(d(agg["combinedMonthlyMwh"]) - 11.13213) < 0.00001, "Combined MWh must be 11.13213");
    UP(d(agg["combinedConnectedCapacityKw"]) == 190, "Combined capacity must be 190 kW");
    UP(d(agg["sumRecordedBillingDemandKw"]) == 123.75, "Sum of billing demand must be 123.75 kW");
    UP(std::fabs(d(agg["sumCurrentBillAmount"]) - 142958.78) < 0.01, "Current bill sum must be ₹142,958.78");
    UP(std::fabs(d(agg["sumPreviousArrears"]) - 71919.43) < 0.01, "Arrears sum must be ₹71,919.43");
    UP(std::fabs(d(agg["effectiveBillRatePerKwh"]) - 142958.78 / 11132.13) < 0.001, "Effective rate must be ~₹12.84/kWh");
}

TEST("upstream energyEvidence: Financial Reconciliation and Arrears Isolation")
{
    const Json& resorts = billFor("prop-vastu-resorts");
    const Json& recon = resorts["financialReconciliation"];
    UP(!recon["isReconciled"].get<bool>(), "Vastu Resorts must be flagged as NOT reconciled");
    UP(std::fabs(d(recon["unreconciledDifference"]) - 2537.69) < 0.01, "Discrepancy must be ₹2,537.69");
    UP(d(recon["arithmeticSum"]) == 133558.31, "Sum of current + arrears must be ₹1,33,558.31");

    const Json& farms = billFor("prop-vastu-farms");
    const Json& abode = billFor("prop-vastu-abode");
    UP(farms["financialReconciliation"]["isReconciled"].get<bool>(), "Vastu Farms must be reconciled");
    UP(d(farms["financialReconciliation"]["unreconciledDifference"]) == 0, "Farms discrepancy must be 0");
    UP(abode["financialReconciliation"]["isReconciled"].get<bool>(), "Vastu Abode must be reconciled");
    UP(d(abode["financialReconciliation"]["unreconciledDifference"]) == 0, "Abode discrepancy must be 0");

    UP(d(resorts["currentBillAmount"]) < d(resorts["totalAmountPayable"]), "Current charges must exclude arrears");
    UP(d(resorts["effectiveRatePerKwh"]) == d(resorts["currentBillAmount"]) / d(resorts["consumptionKwh"]),
       "Effective rate must be based solely on current billing charges");
}

TEST("upstream energyEvidence: Meter Validation")
{
    const Json& farms = billFor("prop-vastu-farms")["meterReadings"];
    UP(std::fabs((d(farms["currentReadingKwh"]) - d(farms["previousReadingKwh"])) - 496.8) < 0.001, "Meter difference must be ~496.8");
    UP(d(farms["inferredMultiplier"]) == 3.0, "Multiplier must be 3.0");
    UP(farms["readingMatchesPrinted"].get<bool>(), "Multiplied reading matches printed consumption exactly");

    const Json& resorts = billFor("prop-vastu-resorts")["meterReadings"];
    UP(std::fabs((d(resorts["currentReadingKwh"]) - d(resorts["previousReadingKwh"])) - 4949.7) < 0.01, "Meter delta must be 4,949.7 kWh");
    UP(!resorts["readingMatchesPrinted"].get<bool>(), "Must flag mismatch between readings and printed consumption");
    UP(std::fabs(d(resorts["readingDiscrepancyKwh"]) - 0.6) < 0.01, "Discrepancy must be 0.6 kWh");

    const Json& abode = billFor("prop-vastu-abode")["meterReadings"];
    UP(std::fabs((d(abode["currentReadingKwh"]) - d(abode["previousReadingKwh"])) - 4692.63) < 0.01, "Meter delta matches printed consumption exactly");
    UP(abode["readingMatchesPrinted"].get<bool>(), "Reading matches printed flag is true");
    UP(d(abode["readingDiscrepancyKwh"]) == 0, "Zero discrepancy");
}

TEST("upstream energyEvidence: Baseline Calibration")
{
    const Json recon = dhun::reconcileBaselineWithEvidence(250000, 10.0);
    UP(d(recon["dhunDerivedMonthlyKwh"]) == 25000, "Derived monthly must be 25,000 kWh");
    UP(d(recon["dhunDerivedAnnualMwh"]) == 300, "Derived annual must be 300 MWh/yr");
    UP(std::fabs(d(recon["referenceAnnualMwh"]) - 133.58556) < 0.01, "Reference annualised must be ~133.59 MWh/yr");
    UP(d(recon["consumptionDifferenceMwh"]) > 166, "Dhun assumption exceeds reference sum by >166 MWh");
    UP(d(recon["referenceEffectiveRate"]) > 12.84 && d(recon["referenceEffectiveRate"]) < 12.85, "Reference effective rate is ~₹12.84/kWh");
    UP(d(recon["tariffDifferencePerKwh"]) < -2.8, "Dhun tariff is ~₹2.84/kWh lower than reference");
    UP(recon["verdict"] == "INCOMPARABLE", "Reconciliation verdict is INCOMPARABLE (unmetered site)");
}

TEST("upstream energyEvidence: Campus Stacked Energy Model")
{
    const Json pilot = dhun::calculateStackedCampusDemand(300, 524, true, 28.47, "Compute Pilot (5 kW)");
    UP(std::fabs(d(pilot["totalCampusDemandMwh"]) - 852.47) < 0.01, "Pilot total must be 852.47 MWh");
    UP(d(pilot["totalElectricityCostLakh"]) == 85.247, "Annual cost at ₹10/kWh is ₹85.247 Lakh");

    const Json p1 = dhun::calculateStackedCampusDemand(300, 524, true, 227.76, "Phase 1 Reference (20 kW)");
    UP(std::fabs(d(p1["totalCampusDemandMwh"]) - 1051.76) < 0.01, "Phase 1 total must be 1,051.76 MWh");
    UP(d(p1["totalElectricityCostLakh"]) == 105.176, "Annual cost at ₹10/kWh is ₹105.176 Lakh");

    const Json exp = dhun::calculateStackedCampusDemand(300, 524, true, 569.4, "Expansion Scenario (50 kW)");
    UP(std::fabs(d(exp["totalCampusDemandMwh"]) - 1393.4) < 0.01, "Expansion total must be 1,393.4 MWh");
    UP(d(exp["totalElectricityCostLakh"]) == 139.34, "Annual cost at ₹10/kWh is ₹139.34 Lakh");

    const Json without = dhun::calculateStackedCampusDemand(300, 524, false, 227.76, "Phase 1 Reference (20 kW)");
    UP(std::fabs(d(without["totalCampusDemandMwh"]) - 527.76) < 0.01, "Excluding planned buildings must yield 527.76 MWh");
}

TEST("upstream energyEvidence: Privacy: Consumer account numbers are masked for UI display")
{
    UP(dhun::maskAccountNumber("05910044") == "•••0044", "Masks account number to last 4 digits");
    UP(dhun::maskAccountNumber("210532020051") == "•••0051", "Masks connection ID to last 4 digits");
}

/* ---------------- assetRegistry.test.ts ---------------- */

TEST("upstream assetRegistry: Asset Registration: enforces required fields (ID, Name, Domain, Type)")
{
    auto repo = freshRepo();
    const auto result = repo.add(Json::parse(R"({"id":"","name":"","domain":"Energy","type":"","ratedCapacity":100,"capacityUnit":"kW"})"));
    UP(!result.success, "Registration must fail when mandatory fields are missing");
    UP(!result.error.empty(), "Registration must return error explanation");
}

TEST("upstream assetRegistry: Asset Registration: rejects duplicate asset IDs")
{
    auto repo = freshRepo();
    Json duplicate = repo.getAll()[0];
    duplicate["name"] = "Duplicate Solar Array";
    const auto result = repo.add(duplicate);
    UP(!result.success, "Registration must reject duplicate asset ID");
    UP(result.error.find("already exists") != std::string::npos, "Error message must cite ID conflict");
}

TEST("upstream assetRegistry: Asset Registration: accepts valid asset and preserves unknown fields explicitly")
{
    auto repo = freshRepo();
    const Json asset = Json::parse(R"({
        "id": "water-tank-sw-02", "name": "SW Bioreserve Retention Basin 2", "domain": "Water",
        "type": "Rainwater Basin", "description": "Rainwater harvesting pond in southern watershed",
        "commissioningStatus": "Planned", "ratedCapacity": 50000, "capacityUnit": "kL",
        "channels": [{"id": "level", "name": "Water Level", "unit": "kL", "dataType": "numeric",
                      "source": "physical_sensor", "expectedSamplingIntervalSeconds": 60}],
        "evidenceClassification": "Reported", "verificationStatus": "unverified",
        "sourceDocument": "Dhun Water Masterplan Rev 4",
        "location": {"parcelName": "SW Bioreserve Parcel 3", "hasVerifiedCoordinates": true, "mapPosition": {"x": 300, "y": 700}},
        "carbonAccounting": {"includedInCampusMeter": false, "eligibleForLedger": false, "emissionFactor": 0}})");
    UP(repo.add(asset).success, "Valid asset should register successfully");
    const auto stored = repo.getById("water-tank-sw-02");
    UP(stored.has_value(), "Asset should be retrievable by ID");
    UP(stored && !stored->contains("uncertaintyNotes"), "Unknown fields must remain undefined, never defaulted");
}

TEST("upstream assetRegistry: Persistence Layer: initializes seed inventory and persists mutations")
{
    auto repo = freshRepo();
    const std::size_t initial = repo.getAll().size();
    UP(initial == dhun::initialSeedAssets().size(), "Seed assets should match expected inventory size");

    Json asset = repo.getAll()[0];
    asset["description"] = "Updated engineering notes";
    UP(repo.update(asset).success, "Update must succeed");
    const std::string id = asset["id"].get<std::string>();
    UP(repo.getById(id).value_or(Json())["description"] == "Updated engineering notes", "Update must persist");

    UP(repo.remove(id), "Delete must return true");
    UP(!repo.getById(id).has_value(), "Deleted asset must not exist");
    UP(repo.getAll().size() == initial - 1, "Inventory length must decrement");
}

TEST("upstream assetRegistry: Persistence Layer: JSON export and import validation")
{
    auto repo = freshRepo();
    const std::string exported = repo.exportJson();
    UP(exported.size() > 100, "Export should produce formatted JSON");
    UP(!repo.importJson("{ not valid json").success, "Import must reject corrupted JSON");
    const auto ok = repo.importJson(exported);
    UP(ok.success, "Import should accept valid asset export");
    UP(ok.count && static_cast<std::size_t>(*ok.count) == dhun::initialSeedAssets().size(), "Import count should match");
}

TEST("upstream assetRegistry: Telemetry: rejects malformed or non-finite readings")
{
    const double now = dhun::wallClockMs();
    Json bad1 = Json::parse(R"({"assetId":"","channelId":"power","value":120,"unit":"kW","source":"physical_sensor","quality":"good"})");
    bad1["timestamp"] = dhun::js::toISOString(now);
    UP(!dhun::validateTelemetryReading(bad1)["valid"].get<bool>(), "Empty assetId must fail validation");

    Json bad2 = Json::parse(R"({"assetId":"solar-pv-01","channelId":"power","unit":"kW","source":"physical_sensor","quality":"good"})");
    bad2["value"] = std::numeric_limits<double>::quiet_NaN();
    bad2["timestamp"] = dhun::js::toISOString(now);
    UP(!dhun::validateTelemetryReading(bad2)["valid"].get<bool>(), "NaN reading value must fail validation");
}

TEST("upstream assetRegistry: Telemetry: detects stale data using 45s TTL threshold")
{
    const double now = dhun::wallClockMs();
    UP(!dhun::detectStaleTelemetry(Json(dhun::js::toISOString(now - 10000)), now, 45), "Reading 10s old must not be flagged as stale under 45s TTL");
    UP(dhun::detectStaleTelemetry(Json(dhun::js::toISOString(now - 50000)), now, 45), "Reading 50s old must be detected as stale under 45s TTL");
}

TEST("upstream assetRegistry: Telemetry: evaluates data quality states (VALID, OUT_OF_RANGE, STALE)")
{
    const double now = dhun::wallClockMs();
    const std::string iso = dhun::js::toISOString(now);
    UP(dhun::evaluateReadingQuality(350, 0, 500, iso, now, 45) == "VALID", "Normal in-range reading should have VALID quality");
    UP(dhun::evaluateReadingQuality(600, 0, 500, iso, now, 45) == "OUT_OF_RANGE", "Exceeded reading should have OUT_OF_RANGE quality");
    UP(dhun::evaluateReadingQuality(350, 0, 500, dhun::js::toISOString(now - 60000), now, 45) == "STALE", "Old reading timestamp should have STALE quality");
}

TEST("upstream assetRegistry: Telemetry: CSV adapter correctly parses tabular channel streams")
{
    const Json parsed = dhun::parseTelemetryCsv(
        "assetId,channelId,value,unit,timestamp,source\n"
        "solar-pv-01,power_kw,412.5,kW,2026-10-29T11:00:00Z,physical_sensor\n"
        "bess-01,state_of_charge_pct,88.2,%,2026-10-29T11:00:00Z,gateway",
        dhun::wallClockMs());
    UP(parsed.size() == 2, "Should parse 2 valid telemetry rows");
    UP(parsed[0]["assetId"] == "solar-pv-01", "Row 1 assetId must match");
    UP(d(parsed[0]["value"]) == 412.5, "Row 1 value must match");
    UP(parsed[1]["channelId"] == "state_of_charge_pct", "Row 2 channelId must match");
}

TEST("upstream assetRegistry: Alert Engine: generates alarms on operational limits violation")
{
    const double now = dhun::wallClockMs();
    const Json asset = Json::parse(R"({
        "id": "test-inverter-01", "name": "Central Inverter 1", "domain": "Energy", "type": "Solar Inverter",
        "description": "Test solar inverter", "commissioningStatus": "Active", "ratedCapacity": 500, "capacityUnit": "kW",
        "channels": [{"id": "temperature_c", "name": "Operating Temperature", "unit": "°C", "dataType": "numeric",
                      "source": "physical_sensor", "expectedSamplingIntervalSeconds": 30,
                      "alertThresholds": {"alarmMax": 75, "normalMax": 68, "thresholdSource": "Engineering Limit Specification",
                                          "ruleDescription": "Observed temperature exceeds critical ceiling"}}],
        "evidenceClassification": "Measured", "verificationStatus": "verified",
        "location": {"parcelName": "Solar Array", "hasVerifiedCoordinates": false},
        "carbonAccounting": {"includedInCampusMeter": true, "eligibleForLedger": true}})");
    Json reading = Json::parse(R"({"assetId":"test-inverter-01","channelId":"temperature_c","value":78.5,"unit":"°C","source":"physical_sensor","quality":"good"})");
    reading["timestamp"] = dhun::js::toISOString(now);
    Json readings;
    readings["temperature_c"] = reading;

    const Json alerts = dhun::evaluateAssetAlerts(asset, readings, now);
    UP(alerts.size() == 1, "Should generate exactly 1 alert for temperature exceeding 75°C");
    UP(alerts.size() == 1 && alerts[0]["severity"] == "alarm", "Alert severity must be alarm");
    UP(alerts.size() == 1 && d(alerts[0]["observedValue"]) == 78.5, "Observed value must match reading");
    UP(alerts.size() == 1 && !alerts[0]["isRegulatory"].get<bool>(), "Must be classified as Operating alert, not regulatory violation");
}

TEST("upstream assetRegistry: Alert Engine: hysteresis deadband prevents alert flapping near boundaries")
{
    UP(dhun::isAlertSuppressedByHysteresis(74.5, 75, 0.02, true), "Hysteresis must hold alert active until value drops below the deadband margin");
    UP(!dhun::isAlertSuppressedByHysteresis(72.0, 75, 0.02, true), "Value below deadband margin must allow alert recovery");
}

TEST("upstream assetRegistry: Alert Engine: regulatory envelope limits are documented and distinct")
{
    const Json& limits = dhun::regulatoryLimits();
    UP(d(limits["MAX_BUILDING_HEIGHT_METERS"]) == 12, "Building height limit must be 12m");
    UP(d(limits["MAX_CAMPUS_CONSTRUCTION_ACRES"]) == 100, "Construction ceiling must be 100 acres");
    UP(d(limits["BIORESERVE_MIN_ACRES"]) == 400, "Bioreserve minimum must be 400 acres");
    UP(d(limits["MAX_ANNUAL_WATER_WITHDRAWAL_KL"]) == 550000, "Water boundary must be 550,000 kL");
}

TEST("upstream assetRegistry: Dashboard Integration: Map markers require verified coordinates")
{
    const Json assets = Json::parse(R"([
        {"id": "located-asset", "location": {"parcelName": "Zone A", "hasVerifiedCoordinates": true, "mapPosition": {"x": 200, "y": 300}}},
        {"id": "unlocated-asset", "location": {"parcelName": "Substation Cabinet B", "hasVerifiedCoordinates": false}}])");
    Json eligible = Json::array();
    for (const auto& a : assets) {
        if (a["location"]["hasVerifiedCoordinates"].get<bool>() && a["location"].contains("mapPosition")) eligible.push_back(a);
    }
    UP(eligible.size() == 1, "Only assets with verified coordinates may be mapped");
    UP(eligible.size() == 1 && eligible[0]["id"] == "located-asset", "Unlocated asset must not be placed at invented map coordinates");
}

TEST("upstream assetRegistry: Carbon Accounting: prevents double-counting of submetered electrical assets")
{
    const Json& assets = dhun::initialSeedAssets();
    const Json* grid = nullptr;
    const Json* solar = nullptr;
    int ledgerEligible = 0, submetered = 0;
    for (const auto& a : assets) {
        if (a["id"] == "grid-hub") grid = &a;
        if (a["id"] == "solar-central") solar = &a;
        if (a.contains("carbonAccounting")) {
            ledgerEligible += a["carbonAccounting"].value("eligibleForLedger", false) ? 1 : 0;
            submetered += a["carbonAccounting"].value("includedInCampusMeter", false) ? 1 : 0;
        }
    }
    UP(grid != nullptr, "Grid substation boundary meter must exist");
    UP(grid && (*grid)["carbonAccounting"]["eligibleForLedger"] == true, "Boundary meter directly calculates campus emissions");
    UP(solar != nullptr, "Solar asset must exist in seed registry");
    UP(solar && (*solar)["carbonAccounting"]["includedInCampusMeter"] == true, "Sub-asset must be marked includedInCampusMeter: true to avoid double counting");
    UP(ledgerEligible > 0, "Must have defined ledger-eligible assets");
    UP(submetered > 0, "Must have tracked submetered components with double counting flag");
}

TEST("upstream assetRegistry: Scale Engine: generates exact counts at 0, 1, 10, 100, 1,000, and 5,000 assets")
{
    for (int count : {0, 1, 10, 100, 1000, 5000}) {
        const Json dataset = dhun::generateTestDataset(count, dhun::wallClockMs());
        const Json& assets = dataset["assets"];
        UP(static_cast<int>(assets.size()) == count, "Generated dataset size must equal requested count " + std::to_string(count));
        std::set<std::string> ids;
        bool allTest = true;
        for (const auto& a : assets) {
            ids.insert(a["id"].get<std::string>());
            allTest = allTest && a["isTestAsset"] == true;
        }
        UP(static_cast<int>(ids.size()) == count, "All " + std::to_string(count) + " assets must possess unique identifiers with zero collisions");
        if (count > 0) UP(allTest, "All synthetic assets must be flagged isTestAsset: true");
    }
}

TEST("upstream assetRegistry: Scale Performance: benchmarks 1,000 assets search, filter, and sort in <50ms")
{
    const Json dataset = dhun::generateTestDataset(1000, dhun::wallClockMs());
    const Json bench = dhun::benchmarkRegistryPerformance(dataset["assets"]);
    UP(d(bench["datasetSize"]) == 1000, "Benchmark must measure 1,000 assets");
    UP(d(bench["filterTimeMs"]) < 50, "Filter time must be under 50ms");
    UP(d(bench["searchTimeMs"]) < 50, "Search time must be under 50ms");
    UP(d(bench["sortTimeMs"]) < 50, "Sort time must be under 50ms");
}

TEST("upstream assetRegistry: Standard Templates: verifies blueprints for all 8 Dhun asset types")
{
    const char* required[] = {"Electrical Meter",      "Solar Inverter",        "Battery Energy Storage",
                              "Water Meter & Pump",    "Water Treatment Plant", "Building Node",
                              "Compute Cluster Node",  "Environmental Sensor Station"};
    const Json templates = dhun::allAssetTemplates();
    for (const char* type : required) {
        const Json* tmpl = nullptr;
        for (const auto& t : templates) {
            if (t["type"] == type) {
                tmpl = &t;
                break;
            }
        }
        UP(tmpl != nullptr, std::string("Standard template must exist for: ") + type);
        if (!tmpl) continue;
        UP(!(*tmpl)["channels"].empty(), std::string("Template ") + type + " must declare standard monitoring channels");

        Json params;
        params["id"] = "test-" + dhun::js::toLower((*tmpl)["domain"].get<std::string>()) + "-01";
        params["name"] = "Test " + (*tmpl)["name"].get<std::string>();
        params["locationParcel"] = "Campus Center";
        params["coordinates"] = Json::parse(R"({"x":100,"y":100})");
        const Json instance = dhun::createAssetFromTemplate(*tmpl, params, dhun::wallClockMs());
        UP(instance["channels"].size() == (*tmpl)["channels"].size(), "Instance must preserve channel count");
        bool units = true;
        for (const auto& ch : instance["channels"]) units = units && dhun::truthyKey(ch, "unit");
        UP(units, "Every instantiated channel must have an explicit unit");
    }
}

TEST("upstream assetRegistry: Lifecycle & Connectivity: active asset can be disconnected, planned cannot be live")
{
    Json active = dhun::initialSeedAssets()[0];
    active["id"] = "active-disc-test";
    active["lifecycleStatus"] = "Active";
    active["connectivityStatus"] = "Disconnected";
    active["monitoringMode"] = "Disconnected";
    UP(active["lifecycleStatus"] == "Active", "Lifecycle can remain Active");
    UP(active["connectivityStatus"] == "Disconnected", "Connectivity can independently be Disconnected");

    Json planned = dhun::initialSeedAssets()[0];
    planned["id"] = "planned-test";
    planned["lifecycleStatus"] = "Planned";
    planned["monitoringMode"] = "Uncommissioned";
    UP(planned["lifecycleStatus"] == "Planned", "Lifecycle is Planned");
    UP(planned["monitoringMode"] != "Live", "Planned asset must never have monitoringMode Live");
}

TEST("upstream assetRegistry: Engineering Validation: Energy Balance verifies supply == demand + storage + losses")
{
    const Json balanced = dhun::validateEnergyBalance(Json::parse(R"({"periodHours":1,"unit":"kW","generation":500,"gridImport":200,"batteryDischarge":100,"demand":780,"batteryCharge":0,"losses":20})"));
    UP(balanced["valid"].get<bool>(), "Balanced energy system must pass validation");
    const Json imbalanced = dhun::validateEnergyBalance(Json::parse(R"({"periodHours":1,"unit":"kW","generation":500,"gridImport":0,"batteryDischarge":0,"demand":900,"batteryCharge":0,"losses":0})"));
    UP(!imbalanced["valid"].get<bool>(), "Energy balance discrepancy must be caught");
    UP(anyIncludes(imbalanced["errors"], "Energy balance violation"), "Error must cite discrepancy");
}

TEST("upstream assetRegistry: Engineering Validation: Water Balance clarifies 550ML catchment vs storage")
{
    const Json ok = dhun::validateWaterBalance(Json::parse(R"({"periodDays":30,"openingStorageKL":50000,"closingStorageKL":65000,"storageCapacityKL":100000,"annualHarvestingPotentialKL":550000,"inflowRainwaterKL":25000,"inflowGroundwaterKL":5000,"inflowRecycledStpKL":2000,"demandCampusKL":8000,"demandIrrigationKL":6000,"evaporationLossesKL":3000})"));
    UP(ok["valid"].get<bool>(), "Consistent water balance must validate");
    const Json conflated = dhun::validateWaterBalance(Json::parse(R"({"periodDays":30,"openingStorageKL":500000,"closingStorageKL":500000,"storageCapacityKL":550000,"inflowRainwaterKL":0,"inflowGroundwaterKL":0,"inflowRecycledStpKL":0,"demandCampusKL":0,"demandIrrigationKL":0,"evaporationLossesKL":0})"));
    UP(anyIncludes(conflated["warnings"], "550 million litre figure is the 500-acre annual catchment"), "Must flag 550ML catchment distinction");
}

TEST("upstream assetRegistry: Engineering Validation: Carbon Boundaries separate standing stock from annual flux")
{
    const Json ok = dhun::validateCarbonBoundaries(Json::parse(R"({"embodiedUpfrontTCO2e":12500,"operationalAnnualTCO2e":340,"storedBiomassStockTCO2e":45000,"annualRemovalsTCO2e":1800,"annualAvoidedGridTCO2e":950,"submeteredBuildingTCO2e":180,"campusSubstationTCO2e":340})"));
    UP(ok["valid"].get<bool>(), "Strict carbon boundary separation must pass");
    const Json bad = dhun::validateCarbonBoundaries(Json::parse(R"({"embodiedUpfrontTCO2e":12500,"operationalAnnualTCO2e":340,"storedBiomassStockTCO2e":45000,"annualRemovalsTCO2e":50000,"annualAvoidedGridTCO2e":950,"submeteredBuildingTCO2e":180,"campusSubstationTCO2e":340})"));
    UP(!bad["valid"].get<bool>(), "Excessive annual removal relative to standing stock must be flagged");
}

TEST("upstream assetRegistry: Engineering Validation: Battery BESS constraints enforce SOC and mutual exclusion")
{
    const Json valid = dhun::validateBatteryDispatch(Json::parse(R"({"nominalCapacityKWh":1000,"usableCapacityKWh":900,"minSocPct":10,"maxSocPct":95,"currentSocPct":75,"chargePowerKW":250,"dischargePowerKW":0,"maxChargeKW":500,"maxDischargeKW":500,"intervalHours":1,"roundTripEfficiency":0.88})"));
    UP(valid["valid"].get<bool>(), "Valid battery dispatch must pass");
    const Json both = dhun::validateBatteryDispatch(Json::parse(R"({"nominalCapacityKWh":1000,"usableCapacityKWh":900,"minSocPct":10,"maxSocPct":95,"currentSocPct":50,"chargePowerKW":200,"dischargePowerKW":200,"maxChargeKW":500,"maxDischargeKW":500,"intervalHours":1})"));
    UP(!both["valid"].get<bool>(), "Simultaneous charge and discharge must fail validation");
    const Json deep = dhun::validateBatteryDispatch(Json::parse(R"({"nominalCapacityKWh":1000,"usableCapacityKWh":900,"minSocPct":10,"maxSocPct":95,"currentSocPct":5,"chargePowerKW":0,"dischargePowerKW":100,"maxChargeKW":500,"maxDischargeKW":500,"intervalHours":1})"));
    UP(!deep["valid"].get<bool>(), "Deep discharge below 10% must fail validation");
}

TEST("upstream assetRegistry: Engineering Validation: Compute Monitoring enforces PUE >= 1.00 and IT load limits")
{
    const Json ok = dhun::validateComputeMonitoring(Json::parse(R"({"itLoadKW":200,"facilityLoadKW":236,"coolingLoadKW":30,"workloadDurationHours":24,"gridCarbonIntensityKgPerKWh":0.82})"));
    UP(ok["valid"].get<bool>(), "Valid compute monitoring must pass");
    UP(d(ok["metadata"]["pue"]) == 1.18, "PUE must calculate correctly as 1.18");
    const Json bad = dhun::validateComputeMonitoring(Json::parse(R"({"itLoadKW":200,"facilityLoadKW":150,"coolingLoadKW":0,"workloadDurationHours":24,"gridCarbonIntensityKgPerKWh":0.82})"));
    UP(!bad["valid"].get<bool>(), "PUE < 1.0 must be rejected as physically impossible");
}

TEST("upstream assetRegistry: Test Lab Isolation: test assets are tagged and purged without data loss to seed records")
{
    const Json& seeds = dhun::initialSeedAssets();
    const Json dataset = dhun::generateTestDataset(100, dhun::wallClockMs());
    Json combined = seeds;
    for (const auto& a : dataset["assets"]) combined.push_back(a);
    UP(combined.size() == seeds.size() + 100, "Combined list contains seed and test assets");

    const Json cleaned = dhun::purgeTestAssetsFromList(combined);
    UP(cleaned.size() == seeds.size(), "Purging must restore exact seed asset count");
    bool noTest = true;
    std::vector<std::string> a, b;
    for (const auto& x : cleaned) {
        noTest = noTest && !dhun::truthyKey(x, "isTestAsset");
        a.push_back(x["id"].get<std::string>());
    }
    for (const auto& x : seeds) b.push_back(x["id"].get<std::string>());
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    UP(noTest, "Zero test assets may remain after purge");
    UP(a == b, "Seed records must remain completely undamaged");
}

TEST("upstream assetRegistry: GIS Engine: Geodesic Haversine distance matches spherical geometry")
{
    const Json& center = dhun::gisLayer("DHUN_SITE_CENTER");
    const double lat1 = d(center[1]);
    const double lng1 = d(center[0]);
    const double lat2 = lat1 + 1000 / 110816.0;
    const double lng2 = lng1;

    const double distance = dhun::calculateGeodesicDistanceMeters(lat1, lng1, lat2, lng2);
    UP(std::fabs(distance - 1000) < 5, "Geodesic distance between site center and 1km north point must be ~1000m");

    const double path = dhun::calculatePathDistanceMeters({{lat1, lng1}, {lat2, lng2}, {lat1, lng1}});
    UP(std::fabs(path - 2000) < 10, "Round-trip path distance must be ~2000m");

    const std::string dms = dhun::formatCoordinatesDMS(lat1, lng1);
    UP(dms.find("26°") != std::string::npos && dms.find("75°") != std::string::npos, "DMS formatter must output degree minutes seconds");
}

TEST("upstream assetRegistry: GIS Engine: Geodesic polygon area calculates correctly for site scale")
{
    const double centerLat = 26.719597;
    const double centerLng = 75.593443;
    const double dLat = 100 / 110816.0;
    const double dLng = 100 / (111412.84 * std::cos((centerLat * 3.141592653589793) / 180));
    const double area = dhun::calculateGeodesicAreaSqm({{centerLat - dLat, centerLng - dLng},
                                                        {centerLat - dLat, centerLng + dLng},
                                                        {centerLat + dLat, centerLng + dLng},
                                                        {centerLat + dLat, centerLng - dLng}});
    UP(std::fabs(area - 40000) < 500, "Calculated area for 200mx200m box must be ~40,000 m²");
}

TEST("upstream assetRegistry: GIS Engine: Point-in-polygon correctly classifies interior and exterior points")
{
    const std::vector<std::pair<double, double>> polygon = {
        {75.590, 26.710}, {75.600, 26.710}, {75.600, 26.725}, {75.590, 26.725}, {75.590, 26.710}};
    UP(dhun::isPointInPolygon({75.595, 26.718}, polygon), "Interior point must be identified as inside polygon");
    UP(!dhun::isPointInPolygon({75.610, 26.730}, polygon), "Exterior point must be identified as outside polygon");
}

TEST("upstream assetRegistry: GIS Boundary Reconciliation: 500-acre masterplan vs 422.01-acre survey are transparently unreconciled")
{
    const Json& r = dhun::gisLayer("BOUNDARY_RECONCILIATION");
    UP(d(r["statedProjectAreaAcres"]) == 500.0, "Stated project area must be exactly 500.00 acres");
    UP(d(r["surveyedClusterAreaAcres"]) == 422.01, "Surveyed cluster land-use schedule must be exactly 422.01 acres");
    UP(std::fabs(d(r["differenceAcres"]) - 77.99) < 0.01, "Area difference must be exactly 77.99 acres");
    UP(r["reconciliationStatus"] == "Documented discrepancy (unreconciled by design)", "Status must explicitly state documented discrepancy unreconciled by design");
    UP(r["sourceDocumentSurvey"].get<std::string>().find("EDS") != std::string::npos, "Survey source must cite the EDS Sustainability KPI Report");
    UP(dhun::gisLayer("PROPERTY_BOUNDARY_500AC")["features"].size() == 1, "500-acre boundary must have exactly 1 polygon feature");
    UP(dhun::gisLayer("SURVEYED_BOUNDARY_422AC")["features"].size() == 1, "422.01-acre boundary must have exactly 1 polygon feature");
}

TEST("upstream assetRegistry: GIS Layer Integrity: All registered layers have documented CRS, authority, and verification status")
{
    const Json& layers = dhun::gisLayer("GIS_LAYERS_REGISTRY");
    UP(layers.size() >= 10, "Registry must contain at least 10 spatial layers");
    for (const auto& layer : layers) {
        const std::string id = layer["id"].get<std::string>();
        UP(!id.empty(), "Layer " + id + " must have a valid ID");
        UP(!layer["name"].get<std::string>().empty(), "Layer " + id + " must have a descriptive name");
        UP(layer["crs"].get<std::string>().find("EPSG:4326") != std::string::npos, "Layer " + id + " must document EPSG:4326 WGS84 CRS");
        UP(!layer["sourceDocument"].get<std::string>().empty(), "Layer " + id + " must cite its source document");
        UP(!layer["verificationStatus"].get<std::string>().empty(), "Layer " + id + " must declare its verification status");
    }
}

TEST("upstream assetRegistry: GIS Asset Integration: Verified coordinates map to Chak Gulabpura, unmapped stay unmapped")
{
    const Json& center = dhun::gisLayer("DHUN_SITE_CENTER");
    int mapped = 0;
    for (const auto& a : dhun::initialSeedAssets()) {
        const Json& loc = a["location"];
        const bool verified = dhun::truthyKey(loc, "hasVerifiedCoordinates");
        const bool coords = loc.contains("coordinates");
        if (verified && coords) {
            ++mapped;
            const double dist = dhun::calculateGeodesicDistanceMeters(d(center[1]), d(center[0]), d(loc["coordinates"]["lat"]),
                                                                      d(loc["coordinates"]["lng"]));
            UP(dist < 2000, "Asset " + a["name"].get<std::string>() + " must be within 2000m of Dhun datum");
        } else {
            UP(!verified, "Unmapped asset " + a["name"].get<std::string>() + " must never have hasVerifiedCoordinates set to true");
        }
    }
    UP(mapped >= 8, "Expected at least 8 mapped seed assets");
}

TEST("upstream suites: assertion count")
{
    /* runs last: every upstream assertion above has executed */
    std::cout << "  (" << upstream::asserts << " upstream assertions checked)\n";
    CHECK(upstream::asserts > 0);
}
