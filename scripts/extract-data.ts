/* ============================================================
   Extract DhunTwin OS reference data into JSON for the core module.

   The original app keeps its catalogues (asset templates, seed
   assets, GIS layers, utility bills, parameter register, ...) as
   TypeScript literals. The core module ships the same values as
   JSON so there is exactly one source of truth: the upstream app.

   Usage:
     DHUN_UPSTREAM=../DhunTwin-OS npx tsx scripts/extract-data.ts
   Writes lib/dhun_engine/data/*.json (sorted keys are NOT applied:
   object key order is the upstream literal order).
   ============================================================ */

import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const upstream = resolve(process.env.DHUN_UPSTREAM ?? join(here, "..", "..", "DhunTwin-OS"));
const dhun = join(upstream, "frontend", "src", "features", "dhun");
const out = join(here, "..", "lib", "dhun_engine", "data");

const load = (rel: string) => import(join(dhun, rel));

function write(name: string, value: unknown): void {
  const file = join(out, `${name}.json`);
  writeFileSync(file, JSON.stringify(value, null, 1) + "\n");
  console.log(`wrote ${file}`);
}

async function main(): Promise<void> {
  mkdirSync(out, { recursive: true });

  const constants = await load("data/constants.ts");
  const phases = await load("data/phases.ts");
  const zones = await load("data/zones.ts");
  const params = await load("data/parameterRegistry.ts");
  const scenarios = await load("engine/scenarios.ts");
  const compute = await load("engine/compute.ts");
  const commons = await load("engine/computeCommonsEngine.ts");
  const energy = await load("energy/energyEvidenceEngine.ts");
  const templates = await load("registry/assetTemplates.ts");
  const seeds = await load("registry/seedAssets.ts");
  const alerts = await load("registry/alertEngine.ts");
  const geo = await load("gis/dhunGeoData.ts");
  const gisMeta = await load("gis/gisMetadata.ts");
  const twinTypes = await load("twin/types.ts");

  /* Scalar constants are compiled into the engine; this file is
     the cross-check the unit tests read. */
  const scalar: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(constants)) {
    scalar[key] = value;
  }

  write("constants", scalar);

  write("phases", {
    PHASES: phases.PHASES,
    PHASE_ORDER: phases.PHASE_ORDER,
    PHASE_START_YEAR: phases.PHASE_START_YEAR,
    MONTH_FACTOR: phases.MONTH_FACTOR,
  });

  write("site_plan", {
    ZONES: zones.ZONES,
    ROADS: zones.ROADS,
    CONTOURS: zones.CONTOURS,
    PONDS: zones.PONDS,
    CANOPY: zones.CANOPY,
  });

  write("parameters", params.PARAMETER_REGISTRY);

  write("scenario_options", {
    COMPUTE_LOAD_KW: scenarios.COMPUTE_LOAD_KW,
    COMPUTE_LABEL: scenarios.COMPUTE_LABEL,
    COOLING_PROFILE: scenarios.COOLING_PROFILE,
    COOLING_LABEL: scenarios.COOLING_LABEL,
  });

  write("compute_queue", {
    KWH_PER_MILLION_TOKENS: compute.KWH_PER_MILLION_TOKENS,
    QUEUE: compute.computeQueue(),
  });

  write("compute_commons", {
    COMPUTE_DEFAULT_SCENARIOS: commons.COMPUTE_DEFAULT_SCENARIOS,
    CAMPUS_ENERGY_DEFAULTS: commons.CAMPUS_ENERGY_DEFAULTS,
    DEFAULT_WORKLOAD_PROFILES: commons.DEFAULT_WORKLOAD_PROFILES,
    COMPUTE_EVIDENCE_REGISTER: commons.COMPUTE_EVIDENCE_REGISTER,
  });

  write("utility_bills", energy.HISTORICAL_REFERENCE_BILLS);

  /* Object.keys order matters to the test lab (round-robin over
     templates), so the catalogue is stored as an ordered list. */
  write(
    "asset_templates",
    Object.entries(templates.ASSET_TEMPLATES).map(([key, template]) => ({
      key,
      template,
    }))
  );

  write("seed_assets", seeds.INITIAL_SEED_ASSETS);
  write("regulatory_limits", alerts.REGULATORY_LIMITS);

  const layers: Record<string, unknown> = {};
  for (const [key, value] of Object.entries(geo)) {
    if (typeof value === "function") continue;
    layers[key] = value;
  }
  write("gis", {
    ...layers,
    BOUNDARY_RECONCILIATION: gisMeta.BOUNDARY_RECONCILIATION,
    GIS_LAYERS_REGISTRY: gisMeta.GIS_LAYERS_REGISTRY,
  });

  write("twin_status", {
    TWIN_STATUS: twinTypes.TWIN_STATUS,
    METRIC_STATUS: twinTypes.METRIC_STATUS,
  });
}

main().catch((error) => {
  console.error(error);
  process.exit(1);
});
