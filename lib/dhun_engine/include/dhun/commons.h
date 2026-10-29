#pragma once

/* ============================================================
   COMPUTE COMMONS, ENERGY EVIDENCE, ENGINEERING VALIDATION
   Ports of engine/computeCommonsEngine.ts,
   energy/energyEvidenceEngine.ts and engine/engineeringValidation.ts.
   ============================================================ */

#include <optional>
#include <string>

#include "dhun/js.h"

namespace dhun {

/* ---------- computeCommonsEngine.ts ---------- */

const Json& computeDefaultScenarios();   // COMPUTE_DEFAULT_SCENARIOS
const Json& campusEnergyDefaults();      // CAMPUS_ENERGY_DEFAULTS
const Json& defaultWorkloadProfiles();   // DEFAULT_WORKLOAD_PROFILES
const Json& computeEvidenceRegister();   // COMPUTE_EVIDENCE_REGISTER

Json calculateComputeScenario(const Json& inputs);

Json calculateCampusEnergyImpact(double computeFacilityMwhYr,
                                 bool includeExistingCampus = true,
                                 bool includePlannedBuildings = true,
                                 std::optional<double> customExistingMwh = std::nullopt,
                                 std::optional<double> customPlannedMwh = std::nullopt);

Json calculateSolarBatteryInteraction(const Json& inputs, const Json& outputs);

/* ---------- energyEvidenceEngine.ts ---------- */

const Json& historicalReferenceBills();  // HISTORICAL_REFERENCE_BILLS

Json calculateAggregateBills(const Json& bills);

Json reconcileBaselineWithEvidence(double dhunMonthlySpend = 250000,
                                   double dhunTariff = 10.0,
                                   const Json* bills = nullptr);

Json calculateStackedCampusDemand(double existingCampusMwh,
                                  double plannedBuildingsMwh,
                                  bool includePlannedBuildings,
                                  double computeFacilityMwh,
                                  const std::string& computeScenarioName,
                                  double effectiveTariff = 10.0,
                                  double gridEmissionFactorKgPerKwh = 0.71);

std::string maskAccountNumber(const std::string& account);

/* ---------- engineeringValidation.ts ---------- */

Json validateEnergyBalance(const Json& params);
Json validateWaterBalance(const Json& params);
Json validateCarbonBoundaries(const Json& params);
Json validateBatteryDispatch(const Json& params);
Json validateComputeMonitoring(const Json& params);

}  // namespace dhun
