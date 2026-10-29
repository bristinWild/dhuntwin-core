#include "dhun/commons.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "dhun/catalog.h"
#include "dhun/constants.h"
#include "dhun/jsonutil.h"

namespace dhun {

namespace {

bool isNaN(double v) { return std::isnan(v); }

/* `Number(x) || 0` */
double numberOrZero(const Json& obj, const char* key)
{
    const double v = numberOf(obj, key);
    return (v != v || v == 0) ? 0 : v;
}

/* `${value}` for a JSON field that may be absent */
std::string interpolate(const Json& obj, const char* key)
{
    if (!obj.is_object() || !obj.contains(key)) return "undefined";
    const Json& v = obj[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number()) return js::toString(v.get<double>());
    if (v.is_null()) return "null";
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return js::stringify(v);
}

Json result(const std::vector<std::string>& errors, const std::vector<std::string>& warnings)
{
    Json j;
    j["valid"] = errors.empty();
    j["errors"] = errors;
    j["warnings"] = warnings;
    return j;
}

}  // namespace

/* ============================================================
   COMPUTE COMMONS  (engine/computeCommonsEngine.ts)
   ============================================================ */

const Json& computeDefaultScenarios() { return catalog::get("compute_commons")["COMPUTE_DEFAULT_SCENARIOS"]; }
const Json& campusEnergyDefaults() { return catalog::get("compute_commons")["CAMPUS_ENERGY_DEFAULTS"]; }
const Json& defaultWorkloadProfiles() { return catalog::get("compute_commons")["DEFAULT_WORKLOAD_PROFILES"]; }
const Json& computeEvidenceRegister() { return catalog::get("compute_commons")["COMPUTE_EVIDENCE_REGISTER"]; }

Json calculateComputeScenario(const Json& inputs)
{
    std::vector<std::string> validationErrors;

    double pue = numberOf(inputs, "pue");
    if (isNaN(pue) || pue < 1.0) {
        validationErrors.push_back(
            "PUE must be >= 1.00 (physically, facility energy cannot be less than IT energy).");
        pue = std::max(1.0, isNaN(pue) ? 1.3 : pue);
    }

    double hoursPerDay = numberOf(inputs, "operatingHoursPerDay");
    if (isNaN(hoursPerDay) || hoursPerDay < 0 || hoursPerDay > 24) {
        validationErrors.push_back("Operating hours per day must be between 0 and 24 hours.");
        hoursPerDay = std::min(24.0, std::max(0.0, isNaN(hoursPerDay) ? 24 : hoursPerDay));
    }

    double daysPerYear = numberOf(inputs, "operatingDaysPerYear");
    if (isNaN(daysPerYear) || daysPerYear < 0 || daysPerYear > 366) {
        validationErrors.push_back("Operating days per year must be between 0 and 366 days.");
        daysPerYear = std::min(366.0, std::max(0.0, isNaN(daysPerYear) ? 365 : daysPerYear));
    }

    double tariff = numberOf(inputs, "electricityRateInrPerKwh");
    if (isNaN(tariff) || tariff < 0) {
        validationErrors.push_back("Electricity tariff must be a non-negative number.");
        tariff = std::max(0.0, isNaN(tariff) ? 10 : tariff);
    }

    double effectiveItLoadKw = 0;
    if (stringOr(inputs, "inputMode") == "ratedUtilisation") {
        const double rated = std::max(0.0, numberOrZero(inputs, "ratedItCapacityKw"));
        const double utilPct = std::min(100.0, std::max(0.0, numberOrZero(inputs, "averageUtilisationPct")));
        effectiveItLoadKw = (rated * utilPct) / 100;
    } else {
        effectiveItLoadKw = std::max(0.0, numberOrZero(inputs, "averageItLoadKw"));
    }

    const double annualOperatingHours = hoursPerDay * daysPerYear;
    const double annualItEnergyKwh = effectiveItLoadKw * annualOperatingHours;
    const double annualItEnergyMwh = annualItEnergyKwh / 1000;

    const double annualFacilityEnergyKwh = annualItEnergyKwh * pue;
    const double annualFacilityEnergyMwh = annualFacilityEnergyKwh / 1000;

    const double annualFacilityOverheadKwh = annualFacilityEnergyKwh - annualItEnergyKwh;
    const double annualFacilityOverheadMwh = annualFacilityOverheadKwh / 1000;

    const double avgFacilityDemandKw = effectiveItLoadKw * pue;

    const double annualElectricityCostInr = annualFacilityEnergyKwh * tariff;
    const double annualElectricityCostLakh = annualElectricityCostInr / 100000;

    const double monthlyFacilityEnergyKwh = annualFacilityEnergyKwh / 12;
    const double monthlyElectricityCostInr = annualElectricityCostInr / 12;
    const double monthlyElectricityCostLakh = monthlyElectricityCostInr / 100000;

    const double annualCarbonEmissionsTons = (annualFacilityEnergyKwh * GRID_FACTOR) / 1000;

    const double pueOverheadPct = (pue - 1.0) * 100;

    Json j;
    j["annualOperatingHours"] = annualOperatingHours;
    j["effectiveItLoadKw"] = effectiveItLoadKw;
    j["annualItEnergyKwh"] = annualItEnergyKwh;
    j["annualItEnergyMwh"] = annualItEnergyMwh;
    j["annualFacilityEnergyKwh"] = annualFacilityEnergyKwh;
    j["annualFacilityEnergyMwh"] = annualFacilityEnergyMwh;
    j["annualFacilityOverheadKwh"] = annualFacilityOverheadKwh;
    j["annualFacilityOverheadMwh"] = annualFacilityOverheadMwh;
    j["avgFacilityDemandKw"] = avgFacilityDemandKw;
    j["annualElectricityCostInr"] = annualElectricityCostInr;
    j["annualElectricityCostLakh"] = annualElectricityCostLakh;
    j["monthlyFacilityEnergyKwh"] = monthlyFacilityEnergyKwh;
    j["monthlyElectricityCostInr"] = monthlyElectricityCostInr;
    j["monthlyElectricityCostLakh"] = monthlyElectricityCostLakh;
    j["annualCarbonEmissionsTons"] = annualCarbonEmissionsTons;
    j["pueOverheadPct"] = pueOverheadPct;
    j["isValid"] = validationErrors.empty();
    j["validationErrors"] = validationErrors;
    return j;
}

Json calculateCampusEnergyImpact(double computeFacilityMwhYr, bool includeExistingCampus,
                                 bool includePlannedBuildings, std::optional<double> customExistingMwh,
                                 std::optional<double> customPlannedMwh)
{
    const Json& defaults = campusEnergyDefaults();

    const double existingMwh =
        includeExistingCampus
            ? (customExistingMwh ? *customExistingMwh : defaults["existingCampusMwhYr"].get<double>())
            : 0;

    const double plannedMwh =
        includePlannedBuildings
            ? (customPlannedMwh ? *customPlannedMwh : defaults["plannedBuildingsMwhYr"].get<double>())
            : 0;

    const double totalModelledCampusMwhYr = existingMwh + plannedMwh + computeFacilityMwhYr;
    const double computeShareOfTotalPct =
        totalModelledCampusMwhYr > 0 ? (computeFacilityMwhYr / totalModelledCampusMwhYr) * 100 : 0;

    Json j;
    j["existingCampusMwhYr"] = existingMwh;
    j["existingCampusCostLakhYr"] = (existingMwh * 1000 * 10) / 100000;
    j["existingCampusDerivation"] = defaults["existingCampusDerivation"];
    j["plannedBuildingsMwhYr"] = plannedMwh;
    j["plannedBuildingsStatus"] = defaults["plannedBuildingsStatus"];
    j["computeFacilityMwhYr"] = computeFacilityMwhYr;
    j["totalModelledCampusMwhYr"] = totalModelledCampusMwhYr;
    j["computeShareOfTotalPct"] = computeShareOfTotalPct;
    j["disclaimer"] =
        "These campus totals are illustrative planning figures. Existing campus consumption is bill-derived, planned buildings is an unvalidated placeholder, and compute is a modelled scenario. Do not treat as metered field data.";
    return j;
}

Json calculateSolarBatteryInteraction(const Json& inputs, const Json& outputs)
{
    const double solarWindowHours = 9;
    const double hoursPerDay = numberOf(inputs, "operatingHoursPerDay");
    const double daysPerYear = numberOf(inputs, "operatingDaysPerYear");

    double daytimeCoincidencePct = 0;
    if (hoursPerDay <= 12) {
        daytimeCoincidencePct = 75.0;
    } else {
        daytimeCoincidencePct = (solarWindowHours / 24) * 100;
    }

    const double facilityMwh = numberOf(outputs, "annualFacilityEnergyMwh");
    const double facilityKwh = numberOf(outputs, "annualFacilityEnergyKwh");

    const double daytimeSolarCoincidentMwh = (facilityMwh * daytimeCoincidencePct) / 100;
    const double nighttimeGridOrBatteryMwh = facilityMwh - daytimeSolarCoincidentMwh;

    const double dailyNighttimeKwh =
        (facilityKwh * (100 - daytimeCoincidencePct)) / 100 / daysPerYear;

    const double nighttimeBatteryCapacityNeededKwh = dailyNighttimeKwh / (0.85 * 0.8);

    Json j;
    j["daytimeCoincidencePct"] = daytimeCoincidencePct;
    j["daytimeSolarCoincidentMwh"] = daytimeSolarCoincidentMwh;
    j["nighttimeGridOrBatteryMwh"] = nighttimeGridOrBatteryMwh;
    j["nighttimeBatteryCapacityNeededKwh"] = nighttimeBatteryCapacityNeededKwh;
    j["coincidentSolarShareText"] =
        hoursPerDay <= 12
            ? "High solar coincidence (~75% daytime operation aligns with site PV array)."
            : "Continuous 24h operation: ~38% aligns with solar window, remaining ~62% requires night grid import or battery discharge.";
    return j;
}

/* ============================================================
   ENERGY EVIDENCE  (energy/energyEvidenceEngine.ts)
   ============================================================ */

const Json& historicalReferenceBills() { return catalog::get("utility_bills"); }

Json calculateAggregateBills(const Json& bills)
{
    double combinedMonthlyKwh = 0;
    double combinedConnectedCapacityKw = 0;
    double sumRecordedBillingDemandKw = 0;
    double sumCurrentBillAmount = 0;
    double sumPreviousArrears = 0;
    int discrepancyCount = 0;

    for (const auto& b : bills) {
        combinedMonthlyKwh += numberOf(b, "consumptionKwh");
        combinedConnectedCapacityKw += numberOf(b, "connectedLoadKw");
        sumRecordedBillingDemandKw += numberOf(b, "recordedBillingDemandKw");
        sumCurrentBillAmount += numberOf(b, "currentBillAmount");
        sumPreviousArrears += numberOf(b, "previousArrears");

        const bool reconciled = b.contains("financialReconciliation") &&
                                truthyKey(b["financialReconciliation"], "isReconciled");
        const bool readingMatches = b.contains("meterReadings") &&
                                    truthyKey(b["meterReadings"], "readingMatchesPrinted");
        if (!reconciled || !readingMatches) ++discrepancyCount;
    }

    const double combinedMonthlyMwh = combinedMonthlyKwh / 1000;
    const double annualisedMwh = combinedMonthlyMwh * 12;
    const double effectiveBillRatePerKwh =
        combinedMonthlyKwh > 0 ? sumCurrentBillAmount / combinedMonthlyKwh : 0;

    Json j;
    j["combinedMonthlyKwh"] = combinedMonthlyKwh;
    j["combinedMonthlyMwh"] = combinedMonthlyMwh;
    j["annualisedMwh"] = annualisedMwh;
    j["combinedConnectedCapacityKw"] = combinedConnectedCapacityKw;
    j["sumRecordedBillingDemandKw"] = sumRecordedBillingDemandKw;
    j["sumCurrentBillAmount"] = sumCurrentBillAmount;
    j["sumPreviousArrears"] = sumPreviousArrears;
    j["effectiveBillRatePerKwh"] = effectiveBillRatePerKwh;
    j["propertyCount"] = bills.size();
    j["hasActiveDiscrepancies"] = discrepancyCount > 0;
    j["discrepancyCount"] = discrepancyCount;
    return j;
}

Json reconcileBaselineWithEvidence(double dhunMonthlySpend, double dhunTariff, const Json* billsIn)
{
    const Json& bills = billsIn ? *billsIn : historicalReferenceBills();

    const double dhunDerivedMonthlyKwh = dhunTariff > 0 ? dhunMonthlySpend / dhunTariff : 0;
    const double dhunDerivedAnnualMwh = (dhunDerivedMonthlyKwh * 12) / 1000;

    const Json agg = calculateAggregateBills(bills);
    const double referenceMonthlyKwh = agg["combinedMonthlyKwh"].get<double>();
    const double referenceAnnualMwh = agg["annualisedMwh"].get<double>();
    const double referenceEffectiveRate = agg["effectiveBillRatePerKwh"].get<double>();

    const double consumptionDifferenceMwh = dhunDerivedAnnualMwh - referenceAnnualMwh;
    const double consumptionDeltaPercent =
        referenceAnnualMwh > 0 ? ((dhunDerivedAnnualMwh - referenceAnnualMwh) / referenceAnnualMwh) * 100 : 0;

    const double tariffDifferencePerKwh = dhunTariff - referenceEffectiveRate;
    const double tariffDeltaPercent =
        referenceEffectiveRate > 0 ? ((dhunTariff - referenceEffectiveRate) / referenceEffectiveRate) * 100 : 0;

    std::string verdict = "PARTIALLY_SUPPORTED";
    std::string verdictRationale;
    std::vector<std::string> recommendations;

    if (bills.empty()) {
        verdict = "UNVERIFIED";
        verdictRationale = "No reference electricity bills registered for calibration.";
        recommendations.push_back("Ingest verified electricity bills to benchmark campus estimates.");
    } else {
        verdict = "INCOMPARABLE";
        verdictRationale =
            "Reference properties show an effective historical rate of ₹12.84/kWh across 190 kW connected capacity (133.59 MWh/yr). Dhun's working assumption of 300 MWh/yr is bill-derived (₹2.5L/mo @ ₹10/kWh) and represents an unmetered site. Denominators (floor area, guest days) are external and cannot be directly equated without verified physical meters.";
        recommendations.push_back(
            "Conduct a physical utility meter audit at Dhun 11kV Substation to establish an empirical baseline.");
        recommendations.push_back(
            "Calibrate working tariff assumption: historical reference bills indicate ₹12.84/kWh all-in cost vs working assumption of ₹10.00/kWh (+28.4% variance).");
        recommendations.push_back(
            "Do not normalize kWh/m² using external property benchmarks without documented architectural floor plans.");
    }

    Json j;
    j["dhunWorkingSpendMonthly"] = dhunMonthlySpend;
    j["dhunWorkingTariff"] = dhunTariff;
    j["dhunDerivedMonthlyKwh"] = dhunDerivedMonthlyKwh;
    j["dhunDerivedAnnualMwh"] = dhunDerivedAnnualMwh;
    j["referenceMonthlyKwh"] = referenceMonthlyKwh;
    j["referenceAnnualMwh"] = referenceAnnualMwh;
    j["referenceEffectiveRate"] = referenceEffectiveRate;
    j["consumptionDifferenceMwh"] = consumptionDifferenceMwh;
    j["consumptionDeltaPercent"] = consumptionDeltaPercent;
    j["tariffDifferencePerKwh"] = tariffDifferencePerKwh;
    j["tariffDeltaPercent"] = tariffDeltaPercent;
    j["verdict"] = verdict;
    j["verdictRationale"] = verdictRationale;
    j["recommendations"] = recommendations;
    return j;
}

Json calculateStackedCampusDemand(double existingCampusMwh, double plannedBuildingsMwh,
                                  bool includePlannedBuildings, double computeFacilityMwh,
                                  const std::string& computeScenarioName, double effectiveTariff,
                                  double gridEmissionFactorKgPerKwh)
{
    const double activePlannedMwh = includePlannedBuildings ? plannedBuildingsMwh : 0;
    const double totalCampusDemandMwh = existingCampusMwh + activePlannedMwh + computeFacilityMwh;
    const double totalCampusKwh = totalCampusDemandMwh * 1000;

    const double totalElectricityCostLakh = (totalCampusKwh * effectiveTariff) / 100000;
    const double gridEmissionsTco2e = (totalCampusKwh * gridEmissionFactorKgPerKwh) / 1000;

    Json j;
    j["existingCampusMwh"] = existingCampusMwh;
    j["existingCampusSource"] = "Bill-derived: ₹2.5 Lakh/mo @ ₹10/kWh (pending 11kV meter audit)";
    j["plannedBuildingsMwh"] = plannedBuildingsMwh;
    j["plannedBuildingsSource"] = "Unvalidated engineering planning placeholder (524 MWh/yr)";
    j["includePlannedBuildings"] = includePlannedBuildings;
    j["computeFacilityMwh"] = computeFacilityMwh;
    j["computeScenarioName"] = computeScenarioName;
    j["totalCampusDemandMwh"] = totalCampusDemandMwh;
    j["totalElectricityCostLakh"] = totalElectricityCostLakh;
    j["blendedEffectiveTariff"] = effectiveTariff;
    j["gridEmissionsTco2e"] = gridEmissionsTco2e;
    return j;
}

std::string maskAccountNumber(const std::string& account)
{
    if (account.size() <= 4) return account;
    return "•••" + account.substr(account.size() - 4);
}

/* ============================================================
   ENGINEERING VALIDATION  (engine/engineeringValidation.ts)
   ============================================================ */

Json validateEnergyBalance(const Json& p)
{
    std::vector<std::string> errors, warnings;
    const double tolerance = numberOr(p, "tolerance", 0.5);

    const double generation = numberOf(p, "generation");
    const double gridImport = numberOf(p, "gridImport");
    const double batteryDischarge = numberOf(p, "batteryDischarge");
    const double demand = numberOf(p, "demand");
    const double batteryCharge = numberOf(p, "batteryCharge");
    const double losses = numberOf(p, "losses");
    const std::string unit = interpolate(p, "unit");

    if (generation < 0) errors.push_back("Generation cannot be negative.");
    if (gridImport < 0) errors.push_back("Grid import cannot be negative (use gridExport for export).");
    if (batteryDischarge < 0) errors.push_back("Battery discharge cannot be negative.");
    if (demand < 0) errors.push_back("Campus demand cannot be negative.");
    if (batteryCharge < 0) errors.push_back("Battery charge cannot be negative.");
    if (losses < 0) errors.push_back("Electrical losses cannot be negative.");

    if (batteryCharge > 0.05 && batteryDischarge > 0.05) {
        errors.push_back("Simultaneous battery charging and discharging detected in the same interval.");
    }

    const double totalInflow = generation + gridImport + batteryDischarge;
    const double totalOutflow = demand + batteryCharge + losses;
    const double delta = std::fabs(totalInflow - totalOutflow);

    if (delta > tolerance) {
        errors.push_back("Energy balance violation: Total Inflow (" + js::toFixed(totalInflow, 2) + " " + unit +
                         ") does not match Total Outflow (" + js::toFixed(totalOutflow, 2) + " " + unit +
                         "). Discrepancy: " + js::toFixed(delta, 2) + " " + unit +
                         " (tolerance: " + js::toString(tolerance) + ").");
    } else if (delta > 0.01) {
        warnings.push_back("Minor rounding delta in energy balance: " + js::toFixed(delta, 4) + " " + unit + ".");
    }

    Json j = result(errors, warnings);
    j["reconciliationDiff"] = delta;
    Json meta;
    meta["totalInflow"] = totalInflow;
    meta["totalOutflow"] = totalOutflow;
    if (defined(p, "unit")) meta["unit"] = p["unit"];
    if (defined(p, "periodHours")) meta["periodHours"] = p["periodHours"];
    j["metadata"] = meta;
    return j;
}

Json validateWaterBalance(const Json& p)
{
    std::vector<std::string> errors, warnings;
    const double tolerance = numberOr(p, "tolerance", 1.0);

    const double closing = numberOf(p, "closingStorageKL");
    const double capacity = numberOf(p, "storageCapacityKL");
    const double opening = numberOf(p, "openingStorageKL");

    if (closing > capacity + 0.1) {
        errors.push_back("Storage overflow: Closing storage (" + js::toFixed(closing, 1) +
                         " kL) exceeds physical reservoir capacity (" + js::toFixed(capacity, 1) + " kL).");
    }
    if (closing < 0) errors.push_back("Closing water storage cannot be negative.");

    const double harvestingEnvelope = numberOr(p, "annualHarvestingPotentialKL", 550000);
    if (capacity >= harvestingEnvelope) {
        warnings.push_back(
            "Storage capacity is configured >= 550,000 kL. Note: The 550 million litre figure is the 500-acre annual catchment runoff envelope, not active structural tank storage.");
    }

    const double totalInflows = numberOf(p, "inflowRainwaterKL") + numberOf(p, "inflowGroundwaterKL") +
                                numberOf(p, "inflowRecycledStpKL");
    const double totalOutflows = numberOf(p, "demandCampusKL") + numberOf(p, "demandIrrigationKL") +
                                 numberOf(p, "evaporationLossesKL");
    const double expectedClosing = opening + totalInflows - totalOutflows;
    const double delta = std::fabs(closing - expectedClosing);

    if (delta > tolerance) {
        errors.push_back("Water mass balance violation: Closing storage (" + js::toFixed(closing, 1) +
                         " kL) does not match expected storage (" + js::toFixed(expectedClosing, 1) +
                         " kL). Discrepancy: " + js::toFixed(delta, 1) + " kL.");
    }

    Json j = result(errors, warnings);
    j["reconciliationDiff"] = delta;
    Json meta;
    meta["totalInflows"] = totalInflows;
    meta["totalOutflows"] = totalOutflows;
    meta["storageUtilizationPct"] = (closing / capacity) * 100;
    meta["annualHarvestingPotentialKL"] = harvestingEnvelope;
    j["metadata"] = meta;
    return j;
}

Json validateCarbonBoundaries(const Json& p)
{
    std::vector<std::string> errors, warnings;

    const double embodied = numberOf(p, "embodiedUpfrontTCO2e");
    const double operational = numberOf(p, "operationalAnnualTCO2e");
    const double stock = numberOf(p, "storedBiomassStockTCO2e");
    const double removals = numberOf(p, "annualRemovalsTCO2e");
    const double submetered = numberOf(p, "submeteredBuildingTCO2e");
    const double substation = numberOf(p, "campusSubstationTCO2e");

    if (removals > stock * 0.2) {
        errors.push_back("Unrealistic biological removal rate: Annual removals (" + js::toFixed(removals, 1) +
                         " tCO2e/yr) cannot exceed 20% of standing biomass stock (" + js::toFixed(stock, 1) +
                         " tCO2e) in temperate/semi-arid drylands.");
    }

    if (submetered > substation + 0.1) {
        errors.push_back("Double-counting boundary violation: Submetered building emissions (" +
                         js::toFixed(submetered, 1) + " tCO2e) exceed total campus substation emissions (" +
                         js::toFixed(substation, 1) + " tCO2e).");
    }

    if (embodied < 0) errors.push_back("Embodied emissions cannot be negative.");
    if (operational < 0) errors.push_back("Operational emissions cannot be negative.");

    Json j = result(errors, warnings);
    Json meta;
    meta["netAnnualFluxTCO2e"] = operational - removals;
    meta["lifetimeStandingStockTCO2e"] = stock;
    meta["totalEmbodiedTCO2e"] = embodied;
    j["metadata"] = meta;
    return j;
}

Json validateBatteryDispatch(const Json& p)
{
    std::vector<std::string> errors, warnings;
    const double efficiency = numberOr(p, "roundTripEfficiency", 0.88);

    const double soc = numberOf(p, "currentSocPct");
    const double minSoc = numberOf(p, "minSocPct");
    const double maxSoc = numberOf(p, "maxSocPct");
    const double charge = numberOf(p, "chargePowerKW");
    const double discharge = numberOf(p, "dischargePowerKW");
    const double maxCharge = numberOf(p, "maxChargeKW");
    const double maxDischarge = numberOf(p, "maxDischargeKW");

    if (soc < minSoc - 0.01) {
        errors.push_back("Battery deep discharge warning: SOC (" + js::toFixed(soc, 1) +
                         "%) is below manufacturer minimum safe limit (" + interpolate(p, "minSocPct") + "%).");
    }
    if (soc > maxSoc + 0.01) {
        errors.push_back("Battery overcharge warning: SOC (" + js::toFixed(soc, 1) +
                         "%) exceeds maximum cycling threshold (" + interpolate(p, "maxSocPct") + "%).");
    }
    if (charge > maxCharge + 0.01) {
        errors.push_back("Charge power (" + js::toFixed(charge, 1) + " kW) exceeds maximum inverter rating (" +
                         interpolate(p, "maxChargeKW") + " kW).");
    }
    if (discharge > maxDischarge + 0.01) {
        errors.push_back("Discharge power (" + js::toFixed(discharge, 1) +
                         " kW) exceeds maximum inverter rating (" + interpolate(p, "maxDischargeKW") + " kW).");
    }
    if (charge > 0.01 && discharge > 0.01) {
        errors.push_back("Simultaneous charge and discharge power commands detected.");
    }
    if (efficiency < 0.70 || efficiency > 0.98) {
        warnings.push_back("Specified BESS efficiency (" + js::toFixed(efficiency * 100, 1) +
                           "%) is outside typical lithium ferro-phosphate range (85-92%).");
    }

    Json j = result(errors, warnings);
    Json meta;
    meta["usableEnergyKWh"] = numberOf(p, "usableCapacityKWh") * (soc / 100);
    meta["efficiency"] = efficiency;
    j["metadata"] = meta;
    return j;
}

Json validateComputeMonitoring(const Json& p)
{
    std::vector<std::string> errors, warnings;

    const double it = numberOf(p, "itLoadKW");
    const double facility = numberOf(p, "facilityLoadKW");
    const double hours = numberOf(p, "workloadDurationHours");
    const double intensity = numberOf(p, "gridCarbonIntensityKgPerKWh");

    if (it < 0) errors.push_back("IT power draw cannot be negative.");
    if (facility < 0) errors.push_back("Facility power draw cannot be negative.");

    if (it > 0 && facility < it - 0.01) {
        errors.push_back("Physical violation: Total Facility Load (" + js::toFixed(facility, 1) +
                         " kW) cannot be less than IT Load (" + js::toFixed(it, 1) + " kW).");
    }

    const double pue = it > 0 ? facility / it : 1.0;
    if (pue < 1.0 - 0.001) {
        errors.push_back("PUE calculation error: PUE (" + js::toFixed(pue, 3) +
                         ") is mathematically impossible (< 1.00).");
    } else if (pue > 2.5) {
        warnings.push_back("Elevated PUE (" + js::toFixed(pue, 2) + "): Significant cooling overhead.");
    }

    const double itEnergyKWh = it * hours;
    const double facilityEnergyKWh = facility * hours;
    const double carbonKg = facilityEnergyKWh * intensity;

    Json j = result(errors, warnings);
    Json meta;
    meta["pue"] = pue;
    meta["itEnergyKWh"] = itEnergyKWh;
    meta["facilityEnergyKWh"] = facilityEnergyKWh;
    meta["carbonKg"] = carbonKg;
    j["metadata"] = meta;
    return j;
}

}  // namespace dhun
