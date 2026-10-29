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
  await twin();
  await registry();
}

/* ---------- asset registry: repository, telemetry, alerts, test lab ---------- */

/* A replay script: every op runs against the upstream classes (and a
   verbatim copy of AssetRegistryContext's derived values); the C++
   parity test replays the same ops through dhun::AssetRegistry. */
export const REGISTRY_OPS: Array<Record<string, unknown>> = [
  { op: "snapshot", at: 1793268000000 },
  { op: "record", at: 1793268000000, params: { assetId: "grid-hub", channelId: "grid-import-kw", value: 320, unit: "kW", source: "gateway" } },
  { op: "record", at: 1793268001000, params: { assetId: "grid-hub", channelId: "grid-frequency", value: 50.3, unit: "Hz", source: "gateway", timestamp: "2026-10-29T10:00:00.500Z" } },
  { op: "record", at: 1793268002000, params: { assetId: "compute-commons", channelId: "compute-it-load-kw", value: 120, source: "gateway" } },
  { op: "record", at: 1793268002000, params: { assetId: "compute-commons", channelId: "compute-it-load-kw", value: 120, source: "gateway", timestamp: 1793268002000 } },
  { op: "record", at: 1793268002000, params: { assetId: "compute-commons", channelId: "compute-it-load-kw", value: 120, source: "gateway", timestamp: 1793268002000 } },
  { op: "record", at: 1793268003000, params: { assetId: "water-stp-central", channelId: "stp-flow-rate", value: 10, source: "physical_sensor" } },
  { op: "record", at: 1793268003000, params: { assetId: "water-stp-central", channelId: "stp-bod", value: 9, source: "manual_entry", timestamp: "2026-10-29T09:58:00Z" } },
  { op: "record", at: 1793268003000, params: { assetId: "bess-storage", channelId: "bess-soc", value: "88", source: "gateway" } },
  { op: "record", at: 1793268003000, params: { assetId: "bess-storage", channelId: "bess-soc", value: 97, source: "gateway", timestamp: 1793268903000 } },
  { op: "record", at: 1793268003000, params: { assetId: "bess-storage", channelId: "bess-soc", value: 97, source: "gateway", timestamp: "garbage" } },
  { op: "snapshot", at: 1793268004000 },
  { op: "record", at: 1793268010000, params: { assetId: "compute-commons", channelId: "compute-it-load-kw", value: 110, source: "gateway" } },
  { op: "snapshot", at: 1793268011000 },
  { op: "record", at: 1793268012000, params: { assetId: "compute-commons", channelId: "compute-it-load-kw", value: 100, source: "gateway" } },
  { op: "snapshot", at: 1793268013000 },
  { op: "snapshot", at: 1793268080000 },
  { op: "csv", at: 1793268090000, assetId: "spirulina-pilot", csv: "channel,value,unit,timestamp\npond-ph,11.5,pH,2026-10-29T10:01:00Z\nspirulina-power-kw,4.2,kW,\n,3,kW,\npond-ph,abc,pH,\npond-ph,9.9,pH,2027-10-29T10:01:00Z\n\nspirulina-power-kw,4.4kW,kW" },
  { op: "csv", at: 1793268090000, assetId: "spirulina-pilot", csv: "metric\n" },
  { op: "csv", at: 1793268090000, assetId: "spirulina-pilot", csv: "foo,bar\n1,2" },
  { op: "history", at: 1793268091000, assetId: "SPIRULINA-pilot", channelId: "pond-ph" },
  { op: "snapshot", at: 1793268091000 },
  { op: "add", at: 1793268100000, template: "ELECTRICAL_METER", params: { id: "  New Meter 01 ", name: " Main Meter ", parcelId: "p2", mapPosition: { x: 120, y: 80 } } },
  { op: "add", at: 1793268100000, template: "SOLAR_INVERTER", params: { id: "inv-02", name: "Inverter 2", coordinates: { lat: 26.7, lng: 75.6 }, commissioningStatus: "Planned", source: "manual_entry" } },
  { op: "add", at: 1793268100000, template: "BATTERY_BESS", params: { id: "new-meter-01", name: "Duplicate" } },
  { op: "addRaw", at: 1793268100000, asset: { id: "x1", name: "No type", domain: "Energy" } },
  { op: "addRaw", at: 1793268100000, asset: { id: "x2", name: "Bad cap", domain: "Energy", type: "Meter", ratedCapacity: -1, capacityUnit: "kW" } },
  { op: "addRaw", at: 1793268100000, asset: { id: "x3", name: "No unit", domain: "Energy", type: "Meter", ratedCapacity: 5, capacityUnit: "  " } },
  { op: "addRaw", at: 1793268100000, asset: { id: "X 4", name: " Raw ", domain: "Water", type: " Pump ", ratedCapacity: 5, capacityUnit: "kL", location: { mapPosition: { x: 0, y: 4 } }, channels: [] } },
  { op: "snapshot", at: 1793268101000 },
  { op: "update", at: 1793268102000, asset: { id: "INV-02", name: "Inverter Two", domain: "Energy", type: "Solar Inverter", commissioningStatus: "Commissioning", location: { hasVerifiedCoordinates: false }, channels: [], ratedCapacity: 10, capacityUnit: "kW" } },
  { op: "update", at: 1793268102000, asset: { id: "missing", name: "x" } },
  { op: "update", at: 1793268102000, asset: { name: "x" } },
  { op: "select", id: "inv-02" },
  { op: "delete", at: 1793268103000, id: "inv-02" },
  { op: "delete", at: 1793268103000, id: "nope" },
  { op: "filter", at: 1793268104000, options: { searchQuery: "solar" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "  SCHNEIDER " } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", domain: "Water" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", status: "Planned" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", operationalState: "stale" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", operationalState: "active_verified" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", telemetrySource: "manual_entry" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", alertSeverity: "stale" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", alertSeverity: "watch" } },
  { op: "filter", at: 1793268104000, options: { searchQuery: "", hasCoordinatesOnly: true, domain: "all" } },
  { op: "export", at: 1793268105000 },
  { op: "import", at: 1793268106000, text: "not json" },
  { op: "import", at: 1793268106000, text: "{\"a\":1}" },
  { op: "import", at: 1793268106000, text: "[{\"id\":\"a\",\"name\":\"A\"}]" },
  { op: "import", at: 1793268106000, text: "[{\"name\":\"A\",\"domain\":\"Energy\"}]" },
  { op: "import", at: 1793268106000, text: "[{\"id\":\"imp-1\",\"name\":\"Imported\",\"domain\":\"Energy\",\"type\":\"Meter\",\"commissioningStatus\":\"Active\",\"channels\":[{\"id\":\"c1\",\"name\":\"C1\",\"unit\":\"kW\",\"source\":\"gateway\",\"alertThresholds\":{\"alarmMax\":5,\"thresholdSource\":\"CERC grid code\"},\"lastReading\":{\"assetId\":\"imp-1\",\"channelId\":\"c1\",\"value\":7,\"unit\":\"kW\",\"timestamp\":\"2026-10-29T10:01:40Z\",\"source\":\"gateway\",\"quality\":\"good\"}}],\"location\":{\"hasVerifiedCoordinates\":false}}]" },
  { op: "snapshot", at: 1793268107000 },
  { op: "reset", at: 1793268108000 },
  { op: "snapshot", at: 1793268108000 },
  { op: "testlab", at: 1793268200000, count: 37 },
  { op: "snapshot", at: 1793268200000 },
  { op: "filter", at: 1793268200000, options: { searchQuery: "05" } },
  { op: "purge", at: 1793268201000 },
  { op: "snapshot", at: 1793268201000 },
  { op: "testlab", at: 1793268202000, count: 0 },
  { op: "snapshot", at: 1793268202000 },
  { op: "addRaw", at: 1793268203000, asset: { id: "solo", name: "Solo", domain: "Energy", type: "Meter", ratedCapacity: 1, capacityUnit: "kW" } },
  { op: "purge", at: 1793268204000 },
  { op: "testlab", at: 1793268205000, count: 1000 },
  { op: "counts", at: 1793268205000 },
  { op: "purge", at: 1793268206000 },
  { op: "snapshot", at: 1793268206000 },
];

async function registry(): Promise<void> {
  /* the browser's localStorage, so the repository runs its real path */
  const store = new Map<string, string>();
  (globalThis as Record<string, unknown>).window = {
    localStorage: {
      getItem: (k: string) => (store.has(k) ? store.get(k)! : null),
      setItem: (k: string, v: string) => void store.set(k, String(v)),
      removeItem: (k: string) => void store.delete(k),
    },
  };

  /* a controllable clock: Date.now() and new Date() */
  const RealDate = Date;
  let fakeNow = 0;
  class FakeDate extends RealDate {
    constructor(...args: unknown[]) {
      if (args.length === 0) super(fakeNow);
      else super(...(args as [number]));
    }
    static now() {
      return fakeNow;
    }
  }
  (globalThis as Record<string, unknown>).Date = FakeDate;

  const repoMod = await load("registry/AssetRepository.ts");
  const telMod = await load("registry/telemetryEngine.ts");
  const alertMod = await load("registry/alertEngine.ts");
  const labMod = await load("registry/testLab.ts");
  const tplMod = await load("registry/assetTemplates.ts");

  const repo = new repoMod.LocalAssetRepository("golden_registry");
  const telemetryEngine = new telMod.TelemetryEngine();
  const alertEngine = new alertMod.AlertEngine();

  type Asset = Record<string, any>;
  let assets: Asset[] = repo.getAll();
  let selected: string | null = null;
  let testBenchmark: Record<string, unknown> | null = null;
  const refresh = () => {
    assets = [...repo.getAll()];
  };
  const getReading = (a: string, c: string) => telemetryEngine.getReading(a, c);
  const opState = (a: Asset) => telemetryEngine.determineOperationalState(a);
  const alerts = () => alertEngine.evaluateAll(assets, getReading);

  /* AssetRegistryContext.tsx, verbatim */
  const counts = () => {
    const current = alerts();
    const assetIdsWithAlerts = new Set(current.map((al: Asset) => al.assetId.toLowerCase()));
    const testAssetCount = assets.filter((a) => a.isTestAsset).length;
    return {
      totalCount: assets.length,
      activeCount: assets.filter((a) => a.lifecycleStatus === "Active" || a.commissioningStatus === "Active").length,
      liveCount: assets.filter((a) => a.monitoringMode === "Live" || opState(a) === "active_verified").length,
      simulatedCount: assets.filter(
        (a) => a.monitoringMode === "Simulated" || opState(a) === "simulated" || a.channels.some((c: Asset) => c.source === "simulation")
      ).length,
      staleCount: assets.filter((a) => a.monitoringMode === "Stale" || a.monitoringMode === "Disconnected" || opState(a) === "stale").length,
      commissioningCount: assets.filter(
        (a) => a.lifecycleStatus === "Commissioning" || a.commissioningStatus === "Commissioning" || opState(a) === "commissioning"
      ).length,
      alertingCount: assets.filter((a) => assetIdsWithAlerts.has(a.id.toLowerCase())).length,
      testAssetCount,
      isTestModeActive: testAssetCount > 0,
    };
  };
  const filterAssets = (options: Asset) => {
    const query = (options.searchQuery || "").trim().toLowerCase();
    return assets.filter((asset) => {
      if (query) {
        const matchName = asset.name.toLowerCase().includes(query);
        const matchId = asset.id.toLowerCase().includes(query);
        const matchType = asset.type.toLowerCase().includes(query);
        const matchSubtype = asset.assetSubtype?.toLowerCase().includes(query) ?? false;
        const matchDomain = asset.domain.toLowerCase().includes(query);
        const matchParcel = asset.location.parcelName?.toLowerCase().includes(query) ?? false;
        const matchMfr = asset.manufacturer?.toLowerCase().includes(query) ?? false;
        const matchModel = asset.model?.toLowerCase().includes(query) ?? false;
        if (!matchName && !matchId && !matchType && !matchSubtype && !matchDomain && !matchParcel && !matchMfr && !matchModel) return false;
      }
      if (options.domain && options.domain !== "all" && asset.domain !== options.domain) return false;
      if (options.status && options.status !== "all") {
        if (asset.lifecycleStatus !== options.status && asset.commissioningStatus !== options.status) return false;
      }
      if (options.operationalState && options.operationalState !== "all") {
        if (opState(asset) !== options.operationalState) return false;
      }
      if (options.telemetrySource && options.telemetrySource !== "all") {
        if (!asset.channels.some((ch: Asset) => ch.source === options.telemetrySource)) return false;
      }
      if (options.alertSeverity && options.alertSeverity !== "all") {
        if (!alertEngine.getAlertForAsset(asset.id).some((a: Asset) => a.severity === options.alertSeverity)) return false;
      }
      if (options.hasCoordinatesOnly && !asset.location.hasVerifiedCoordinates) return false;
      return true;
    });
  };
  const snapshot = () => ({
    assets,
    alerts: alerts(),
    selectedAssetId: selected,
    counts: counts(),
    /* timings are wall-clock measurements; only the shape is compared */
    testBenchmark: testBenchmark && { datasetSize: testBenchmark.datasetSize, memoryKb: testBenchmark.memoryKb },
  });

  /* Results are captured as they were at that moment: the engines hand
     out live objects (getReading re-grades quality in place). */
  const results = REGISTRY_OPS.map((op) => JSON.parse(JSON.stringify(runOp(op)) ?? "null"));

  function runOp(op: Record<string, any>): unknown {
    if (typeof op.at === "number") fakeNow = op.at;
    switch (op.op) {
      case "snapshot":
        return snapshot();
      case "counts":
        return counts();
      case "record":
        return telemetryEngine.ingest(op.params);
      case "csv":
        return telemetryEngine.parseCsvReadings(op.csv, op.assetId);
      case "history": {
        const a = op.assetId as string, c = op.channelId as string;
        return { history: telemetryEngine.getHistory(a, c), reading: telemetryEngine.getReading(a, c) ?? null };
      }
      case "add": {
        const asset = tplMod.createAssetFromTemplate(op.template, op.params);
        const res = repo.add(asset);
        if (res.success) refresh();
        return { asset, res };
      }
      case "addRaw": {
        const res = repo.add(op.asset);
        if (res.success) refresh();
        return res;
      }
      case "update": {
        const res = repo.update(op.asset);
        if (res.success) refresh();
        return res;
      }
      case "select":
        selected = op.id as string;
        return null;
      case "delete": {
        const res = repo.delete(op.id);
        if (res) {
          refresh();
          if (selected === op.id) selected = null;
        }
        return res;
      }
      case "filter":
        return filterAssets(op.options as Asset).map((a) => a.id);
      case "export":
        return repo.exportJson();
      case "import": {
        const res = repo.importJson(op.text);
        if (res.success) refresh();
        return res;
      }
      case "reset":
        repo.resetToDefaults();
        refresh();
        selected = null;
        testBenchmark = null;
        return null;
      case "testlab": {
        const count = op.count as number;
        if (count === 0) {
          repo.setAssets([]);
          refresh();
          selected = null;
          testBenchmark = { datasetSize: 0, generationTimeMs: 0.1, filterTimeMs: 0.1, searchTimeMs: 0.1, sortTimeMs: 0.1, memoryKb: 0 };
          return null;
        }
        const { assets: testAssets, durationMs } = labMod.generateTestDataset(count);
        const bench = labMod.benchmarkRegistryPerformance(testAssets);
        testBenchmark = {
          datasetSize: count,
          generationTimeMs: durationMs,
          filterTimeMs: bench.filterTimeMs,
          searchTimeMs: bench.searchTimeMs,
          sortTimeMs: bench.sortTimeMs,
          memoryKb: bench.estimatedMemoryKb,
        };
        repo.setAssets(testAssets);
        refresh();
        selected = testAssets[0]?.id ?? null;
        return { datasetSize: count, memoryKb: bench.estimatedMemoryKb };
      }
      case "purge": {
        const removed = repo.purgeTestAssets();
        refresh();
        selected = null;
        testBenchmark = null;
        return removed;
      }
      default:
        throw new Error(`unknown op ${op.op}`);
    }
  }

  /* standalone helpers */
  fakeNow = 1793268300000;
  const helpers = {
    csv: telMod.parseTelemetryCsv("assetId,channel,value,unit,source\nsolar-central,solar-gen-kw,410,kW,gateway\nx,y,nope,kW,\nshort,row"),
    validate: [
      { assetId: "a", channelId: "b", value: 1, timestamp: "t" },
      { assetId: "", channelId: "b", value: 1, timestamp: "t" },
      { assetId: "a", channelId: "b", value: Infinity, timestamp: "t" },
      { assetId: "a", channelId: "b", value: 2 },
    ].map((r) => telMod.validateTelemetryReading(r)),
    stale: ["2026-10-29T10:04:00Z", "2026-10-29T10:05:00Z", "x", 1793268290000].map((t) => telMod.detectStaleTelemetry(t)),
    quality: [
      [5, 0, 10, undefined],
      [-1, 0, 10, undefined],
      [11, 0, 10, undefined],
      [5, undefined, undefined, "2026-10-29T09:00:00Z"],
    ].map((a) => (telMod.evaluateReadingQuality as Function)(...a)),
    hysteresis: [
      [95, 100, 0.05, true],
      [94, 100, 0.05, true],
      [99, 100, 0.05, false],
    ].map((a) => (alertMod.isAlertSuppressedByHysteresis as Function)(...a)),
    perAsset: alertMod.evaluateAssetAlerts(
      repoMod.LocalAssetRepository ? JSON.parse(JSON.stringify((await load("registry/seedAssets.ts")).INITIAL_SEED_ASSETS[3])) : {},
      { "compute-it-load-kw": { assetId: "compute-commons", channelId: "compute-it-load-kw", value: 130, unit: "kW", timestamp: "2026-10-29T10:04:00Z", source: "gateway", quality: "good" } }
    ),
    lab: labMod.generateTestAssets({ count: 12, includeCoordinatesPercentage: 40, includeAlertsPercentage: 50, datasetTag: "golden" }),
    labDefaultTag: labMod.generateTestAssets({ count: 3 }).assets.map((a: Asset) => a.testDatasetId),
    purged: labMod.purgeTestAssetsFromList([{ id: "test-a" }, { id: "b", isTestAsset: true }, { id: "c" }]),
    allTemplates: tplMod.ALL_ASSET_TEMPLATES,
  };
  delete (helpers.lab as Asset).benchmark;

  (globalThis as Record<string, unknown>).Date = RealDate;
  delete (globalThis as Record<string, unknown>).window;

  write("registry", { ops: REGISTRY_OPS, results, helpers });
}

/* ---------- digital twin frames, events, actions, packets ---------- */

async function twin(): Promise<void> {
  const frames = await load("twin/buildFrame.ts");
  const source = await load("twin/source.ts");

  const realNow = Date.now;
  const runAt = <T>(wallNow: number, fn: () => T): T => {
    Date.now = () => wallNow;
    try {
      return fn();
    } finally {
      Date.now = realNow;
    }
  };

  const packetSets = (wallNow: number) => [
    {},
    {
      "grid-hub:import": { nodeId: "grid-hub", metric: "import", value: 318.4, at: wallNow - 1000 },
      "stp:ph": { nodeId: "stp", metric: "ph", value: 9.5, at: wallNow - 5000 },
      "weather:rain": { nodeId: "weather", metric: "rain", value: 42, at: wallNow - 40000 },
      "compute:pue": { nodeId: "compute", metric: "pue", value: 1.9 },
      "irrigation:reservoir": { nodeId: "irrigation", metric: "reservoir", value: 12, at: wallNow - 29999 },
      "solar-array:output": { nodeId: "solar-array", metric: "output", value: 50000, at: wallNow },
    },
  ];

  const cases = PHASES.flatMap((phase) =>
    INSTANTS.slice(0, 12).flatMap((ms, i) => {
      const wallNow = ms + (i % 5) * 700;
      return packetSets(wallNow).map((packets) => {
        const frame = runAt(wallNow, () => frames.buildFrame(phase, new Date(ms), packets));
        return {
          phase,
          ms,
          wallNow,
          packets,
          frame,
          events: frames.buildEvents(frame),
          actions: frames.buildActions(phase, new Date(ms)),
        };
      });
    })
  );

  const raws = [
    '{"nodeId":"grid-hub","metric":"import","value":318.4,"at":1790000000000}',
    '{"nodeId":"grid-hub","metric":"import","value":318.4}',
    '[{"nodeId":"a","metric":"b","value":1},{"nodeId":"","metric":"b","value":1},{"nodeId":"c","metric":"d","value":"7"}]',
    '{"type":"telemetry","packets":[{"nodeId":"stp","metric":"ph","value":7.2,"at":1790000000500},{"packets":[]}]}',
    '{"nodeId":"x","metric":"y","value":1e400}',
    "not json",
    "42",
    "null",
    '{"nodeId":"x","metric":"y","value":2,"at":"soon"}',
  ];

  write("twin", {
    cases,
    packets: raws.map((raw) => ({ raw, parsed: runAt(1790000009999, () => source.parsePacket(raw)) })),
  });
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
