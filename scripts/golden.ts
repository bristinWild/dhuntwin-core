/* ============================================================
   Golden outputs from the ORIGINAL DhunTwin OS TypeScript engine.

   Runs the upstream engine over a fixed matrix of inputs and
   writes tests/golden/*.json. The C++ parity test replays the
   same inputs through dhun_engine and compares field by field
   (strings exactly, numbers to 1e-9 relative).

   Run with the site timezone so the browser-side IST shift is the
   identity:  TZ=Asia/Kolkata npx tsx scripts/golden.ts
   ============================================================ */

import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const upstream = resolve(process.env.DHUN_UPSTREAM ?? join(here, "..", "..", "DhunTwin-OS"));
const dhun = join(upstream, "frontend", "src", "features", "dhun");
const out = join(here, "..", "tests", "golden");
const load = (rel: string) => import(join(dhun, rel));

if (new Date(0).getTimezoneOffset() !== -330) {
  console.error("Run with TZ=Asia/Kolkata so local getters read IST.");
  process.exit(1);
}

const PHASES = ["today", "phase1", "buildout"] as const;

/* Instants spread over a year and over the day, plus edges. */
export const INSTANTS: number[] = (() => {
  const base = Date.UTC(2026, 0, 1, 0, 0, 0);
  const list: number[] = [];
  for (let i = 0; i < 61; i++) {
    list.push(base + i * (6 * 86400000 + 97 * 60000 + 13000));
  }
  list.push(Date.UTC(2026, 9, 29, 15, 39, 5, 7));
  list.push(Date.UTC(2026, 5, 21, 6, 30, 0));
  list.push(Date.UTC(2026, 11, 31, 18, 29, 59, 999));
  return list;
})();

/* Two-digit IST offset string, the C++ engine's clock format. */
function siteIso(ms: number): string {
  const d = new Date(ms);
  const p = (n: number, w = 2) => String(n).padStart(w, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${p(d.getMilliseconds(), 3)}+05:30`;
}

/* Dates become the C++ representation before they are written. */
function normalize(value: unknown): unknown {
  return JSON.parse(
    JSON.stringify(value, function (this: Record<string, unknown>, key, v) {
      const raw = this[key];
      if (raw instanceof Date) return siteIso(raw.getTime());
      return v;
    })
  );
}

function write(name: string, value: unknown): void {
  const file = join(out, `${name}.json`);
  writeFileSync(file, JSON.stringify(normalize(value)) + "\n");
  console.log(`wrote ${file}`);
}

async function main(): Promise<void> {
  mkdirSync(out, { recursive: true });

  const simulation = await load("engine/simulation.ts");

  write("simulation", {
    instants: INSTANTS,
    twins: PHASES.flatMap((phase) =>
      INSTANTS.map((ms) => ({ phase, ms, twin: simulation.buildTwin(phase, new Date(ms)) }))
    ),
    annual: PHASES.map((phase) => ({
      phase,
      energy: simulation.annualEnergy(phase),
      landscape: simulation.landscapeWaterKlYr(phase),
    })),
    shapes: Array.from({ length: 97 }, (_, i) => {
      const f = i / 96;
      return { f, solar: simulation.solarShape(f), grid: simulation.gridIntensity(f) };
    }),
  });

  await planning();
  await commons();
}

/* ---------- ledger, scenarios, compute, zone pressure ---------- */

async function planning(): Promise<void> {
  const ledger = await load("engine/ledger.ts");
  const scenarios = await load("engine/scenarios.ts");
  const compute = await load("engine/compute.ts");
  const zoneStatus = await load("engine/zoneStatus.ts");
  const simulation = await load("engine/simulation.ts");
  const zones = await load("data/zones.ts");

  const ledgerOptions = [
    {},
    { forestAcres: 50 },
    { forestAcres: 0 },
    { residualT: 100 },
    { residualT: -40 },
    { residualT: null },
    { horizonYears: 50 },
    { gridFactor: 0.5, forestAcres: 124, horizonYears: 10 },
  ];

  const scenarioVariants = (base: Record<string, unknown>) => [
    base,
    { ...base, solarKwp: 0 },
    { ...base, solarKwp: 5000, batteryMwh: 2 },
    { ...base, solarKwp: 30000, batteryMwh: 8 },
    { ...base, batteryMwh: 0 },
    { ...base, epi: 35 },
    { ...base, epi: 134 },
    { ...base, gridFactor: 0.4 },
    { ...base, forestAcres: 400 },
    { ...base, forestAcres: 0 },
    { ...base, compute: "none" },
    { ...base, compute: "workstation" },
    { ...base, compute: "server" },
    { ...base, compute: "flagship" },
    { ...base, compute: "flagship", cooling: "evaporative" },
    { ...base, cooling: "evaporative" },
    { ...base, solarKwp: 16816, batteryMwh: 20, forestAcres: 124, gridFactor: 0.2 },
  ];

  const jobs = compute.computeQueue();
  const hours = [...Array.from({ length: 24 }, (_, h) => h), 13.5, 0.25, 23.99];

  write("planning", {
    ledger: PHASES.flatMap((phase) =>
      ledgerOptions.map((options) => ({ phase, options, ledger: ledger.buildLedger(phase, options as never) }))
    ),
    scenarioOptions: {
      COMPUTE_LOAD_KW: scenarios.COMPUTE_LOAD_KW,
      COMPUTE_LABEL: scenarios.COMPUTE_LABEL,
      COOLING_PROFILE: scenarios.COOLING_PROFILE,
      COOLING_LABEL: scenarios.COOLING_LABEL,
    },
    defaults: PHASES.map((phase) => ({ phase, inputs: scenarios.defaultScenario(phase) })),
    scenarios: PHASES.flatMap((phase) =>
      scenarioVariants(scenarios.defaultScenario(phase)).map((inputs) => ({
        phase,
        inputs,
        result: scenarios.runScenario(phase, inputs as never),
      }))
    ),
    intensity: PHASES.flatMap((phase) =>
      hours.map((hour) => ({ phase, hour, value: compute.marginalIntensity(phase, hour) }))
    ),
    windows: PHASES.map((phase) => ({ phase, windows: compute.computeWindows(phase) })),
    queue: jobs,
    jobs: PHASES.flatMap((phase) =>
      jobs.flatMap((job: never) =>
        hours.slice(0, 24).map((hour) => ({
          phase,
          job,
          hour,
          energy: compute.jobEnergyKwh(phase, job),
          carbon: compute.jobCarbon(phase, job, hour),
        }))
      )
    ),
    zones: PHASES.flatMap((phase) =>
      INSTANTS.slice(0, 24).map((ms) => {
        const { summary, assets } = simulation.buildTwin(phase, new Date(ms));
        const pressures = zoneStatus.zonePressures(summary, assets);
        /* the Campus workspace's issue list (pages/Campus.tsx) */
        const issues = zones.ZONES.map((zone: { id: string; anchorAsset: string }) => {
          const level = zoneStatus.zoneLevel(pressures[zone.id]);
          return {
            zone,
            level,
            reason: zoneStatus.topPressure(pressures[zone.id], level).text,
            asset: assets.find((item: { id: string }) => item.id === zone.anchorAsset) ?? null,
          };
        })
          .filter((issue: { level: string }) => issue.level !== "healthy")
          .sort((a: { level: string }, b: { level: string }) =>
            a.level === b.level ? 0 : a.level === "attention" ? -1 : 1
          );
        return { phase, ms, pressures, issues };
      })
    ),
  });
}

/* ---------- compute commons, energy evidence, validation ---------- */

async function commons(): Promise<void> {
  const cc = await load("engine/computeCommonsEngine.ts");
  const ev = await load("energy/energyEvidenceEngine.ts");
  const val = await load("engine/engineeringValidation.ts");

  const base = cc.COMPUTE_DEFAULT_SCENARIOS;
  const scenarioInputs = [
    ...Object.values(base),
    { ...base.custom, inputMode: "ratedUtilisation" },
    { ...base.custom, inputMode: "ratedUtilisation", averageUtilisationPct: 140 },
    { ...base.custom, inputMode: "ratedUtilisation", ratedItCapacityKw: undefined },
    { ...base.pilot, pue: 0.8 },
    { ...base.pilot, pue: "abc" },
    { ...base.pilot, pue: "1.45" },
    { ...base.pilot, operatingHoursPerDay: 30 },
    { ...base.pilot, operatingHoursPerDay: -2 },
    { ...base.pilot, operatingDaysPerYear: 400 },
    { ...base.pilot, electricityRateInrPerKwh: -1 },
    { ...base.pilot, averageItLoadKw: -5 },
    { ...base.pilot, averageItLoadKw: null },
    { ...base.phase1, operatingHoursPerDay: 12 },
    { ...base.expansion, pue: 1.0, electricityRateInrPerKwh: 0 },
  ];

  const computed = scenarioInputs.map((inputs) => {
    const outputs = cc.calculateComputeScenario(inputs);
    return { inputs, outputs, solar: cc.calculateSolarBatteryInteraction(inputs, outputs) };
  });

  const impacts = [
    [28.47, true, true],
    [227.76, true, true],
    [569.4, false, true],
    [569.4, true, false],
    [0, false, false],
    [100, true, true, 250, 600],
    [100, true, true, 0, undefined],
  ].map((args) => ({ args, impact: (cc.calculateCampusEnergyImpact as Function)(...args) }));

  const bills = ev.HISTORICAL_REFERENCE_BILLS;
  const reconcileArgs: unknown[][] = [[], [300000], [250000, 12], [250000, 0], [250000, 10, []], [250000, 10, bills.slice(0, 1)]];

  const energyValid = [
    { periodHours: 1, unit: "kWh", generation: 100, gridImport: 50, batteryDischarge: 0, demand: 140, batteryCharge: 0, losses: 10 },
    { periodHours: 1, unit: "kW", generation: 100, gridImport: 50, batteryDischarge: 5, demand: 120, batteryCharge: 5, losses: 1 },
    { periodHours: 24, unit: "kWh", generation: -1, gridImport: -2, batteryDischarge: -3, demand: -4, batteryCharge: -5, losses: -6 },
    { periodHours: 1, unit: "kWh", generation: 10, gridImport: 0, batteryDischarge: 0, demand: 9.97, batteryCharge: 0, losses: 0 },
    { periodHours: 1, unit: "kWh", generation: 10, gridImport: 0, batteryDischarge: 0, demand: 9, batteryCharge: 0, losses: 0, tolerance: 2 },
  ];
  const waterValid = [
    { periodDays: 30, openingStorageKL: 1000, closingStorageKL: 1100, storageCapacityKL: 5000, inflowRainwaterKL: 300, inflowGroundwaterKL: 0, inflowRecycledStpKL: 50, demandCampusKL: 200, demandIrrigationKL: 40, evaporationLossesKL: 10 },
    { periodDays: 30, openingStorageKL: 1000, closingStorageKL: 6000, storageCapacityKL: 5000, inflowRainwaterKL: 300, inflowGroundwaterKL: 0, inflowRecycledStpKL: 50, demandCampusKL: 200, demandIrrigationKL: 40, evaporationLossesKL: 10 },
    { periodDays: 365, openingStorageKL: 0, closingStorageKL: -5, storageCapacityKL: 600000, inflowRainwaterKL: 0, inflowGroundwaterKL: 0, inflowRecycledStpKL: 0, demandCampusKL: 0, demandIrrigationKL: 0, evaporationLossesKL: 0, tolerance: 10 },
  ];
  const carbonValid = [
    { embodiedUpfrontTCO2e: 1000, operationalAnnualTCO2e: 200, storedBiomassStockTCO2e: 45000, annualRemovalsTCO2e: 1800, annualAvoidedGridTCO2e: 100, submeteredBuildingTCO2e: 50, campusSubstationTCO2e: 200 },
    { embodiedUpfrontTCO2e: -1, operationalAnnualTCO2e: -1, storedBiomassStockTCO2e: 1000, annualRemovalsTCO2e: 1800, annualAvoidedGridTCO2e: 0, submeteredBuildingTCO2e: 500, campusSubstationTCO2e: 200 },
  ];
  const batteryValid = [
    { nominalCapacityKWh: 1000, usableCapacityKWh: 900, minSocPct: 10, maxSocPct: 95, currentSocPct: 50, chargePowerKW: 0, dischargePowerKW: 100, maxChargeKW: 250, maxDischargeKW: 250, intervalHours: 1 },
    { nominalCapacityKWh: 1000, usableCapacityKWh: 900, minSocPct: 10, maxSocPct: 95, currentSocPct: 5, chargePowerKW: 300, dischargePowerKW: 300, maxChargeKW: 250, maxDischargeKW: 250, intervalHours: 1, roundTripEfficiency: 0.6 },
    { nominalCapacityKWh: 1000, usableCapacityKWh: 900, minSocPct: 10, maxSocPct: 95, currentSocPct: 99.5, chargePowerKW: 0, dischargePowerKW: 0, maxChargeKW: 250, maxDischargeKW: 250, intervalHours: 1 },
  ];
  const computeValid = [
    { itLoadKW: 20, facilityLoadKW: 26, coolingLoadKW: 5, workloadDurationHours: 24, gridCarbonIntensityKgPerKWh: 0.71 },
    { itLoadKW: 20, facilityLoadKW: 15, coolingLoadKW: 0, workloadDurationHours: 1, gridCarbonIntensityKgPerKWh: 0.85 },
    { itLoadKW: 10, facilityLoadKW: 30, coolingLoadKW: 18, workloadDurationHours: 2, gridCarbonIntensityKgPerKWh: 0.85 },
    { itLoadKW: 0, facilityLoadKW: 3, coolingLoadKW: 0, workloadDurationHours: 2, gridCarbonIntensityKgPerKWh: 0.85 },
    { itLoadKW: -1, facilityLoadKW: -2, coolingLoadKW: 0, workloadDurationHours: 2, gridCarbonIntensityKgPerKWh: 0.85 },
  ];

  write("commons", {
    defaults: {
      COMPUTE_DEFAULT_SCENARIOS: cc.COMPUTE_DEFAULT_SCENARIOS,
      CAMPUS_ENERGY_DEFAULTS: cc.CAMPUS_ENERGY_DEFAULTS,
      DEFAULT_WORKLOAD_PROFILES: cc.DEFAULT_WORKLOAD_PROFILES,
      COMPUTE_EVIDENCE_REGISTER: cc.COMPUTE_EVIDENCE_REGISTER,
    },
    computed,
    impacts,
    aggregate: [bills, bills.slice(0, 1), []].map((list: unknown[]) => ({ bills: list, aggregate: ev.calculateAggregateBills(list) })),
    reconcile: reconcileArgs.map((args) => ({ args, result: (ev.reconcileBaselineWithEvidence as Function)(...args) })),
    stacked: [
      [300, 524, true, 28.47, "Compute Pilot"],
      [300, 524, false, 227.76, "Phase 1 Reference"],
      [300, 524, true, 569.4, "Expansion Scenario", 12.84],
      [133.59, 0, true, 0, "None", 10, 0.85],
    ].map((args) => ({ args, result: (ev.calculateStackedCampusDemand as Function)(...args) })),
    masks: ["1234", "", "JVVNL-210431987", "12345"].map((acc) => ({ acc, masked: ev.maskAccountNumber(acc) })),
    validation: {
      energy: energyValid.map((params) => ({ params, result: val.validateEnergyBalance(params) })),
      water: waterValid.map((params) => ({ params, result: val.validateWaterBalance(params) })),
      carbon: carbonValid.map((params) => ({ params, result: val.validateCarbonBoundaries(params) })),
      battery: batteryValid.map((params) => ({ params, result: val.validateBatteryDispatch(params) })),
      compute: computeValid.map((params) => ({ params, result: val.validateComputeMonitoring(params) })),
    },
  });
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
