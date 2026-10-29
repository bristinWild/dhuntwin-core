# DhunTwin Core

**The DhunTwin OS engine as a Logos core module.**

DhunTwin OS is an evidence-first digital twin of the Logos × ZuGrama field station at Chak Gulabpura, Jaipur: campus simulation, whole-life carbon ledger, scenario cockpit, compute commons, energy evidence, live telemetry frames and a central asset registry. Upstream it is a React app whose engine runs in the browser ([`tuhin-codes/DhunTwin-OS`](https://github.com/tuhin-codes/DhunTwin-OS)). This repository is the first half of its Basecamp port: **`dhuntwin_core`**, a core module that carries the whole engine, so the UI module (next) only draws.

> **Status:** engine complete and checked field-for-field against the original TypeScript; module glue generated and compiled with the Logos SDK pinned by logos-module-builder 0.3.3. The UI module (`ui_qml`) is the next step.
>
> **Confidentiality:** upstream marks DhunTwin OS as shared project material for the Dhun team. This repository ports its code and data; keep it private unless the Dhun team agrees otherwise.

---

## Contents

1. [Architecture](#1-architecture)
2. [Build and run](#2-build-and-run)
3. [API](#3-api)
4. [Behaviour carried over](#4-behaviour-carried-over)
5. [Parity with the TypeScript engine](#5-parity-with-the-typescript-engine)
6. [Testing](#6-testing)
7. [Differences from the web app](#7-differences-from-the-web-app)
8. [Repository layout](#8-repository-layout)

---

## 1. Architecture

```
┌─ Basecamp ──────────────────────────────────────────────┐
│  dhuntwin (ui_qml, next)   QML views + Qt backend         │
│        │  logos.module / calls + events                   │
└────────┼─────────────────────────────────────────────────┘
┌────────▼────────────────────────────────────────────────┐
│ logos_host: dhuntwin_core (this repo)                     │
│   generated glue (Qt plugin + C ABI)                      │
│   src/dhuntwin_core_impl  ← the module API, ~60 methods   │
│   lib/dhun_engine         ← the engine: C++20, no Qt      │
│     data/*.json           ← upstream catalogues, embedded │
└──────────────────────────────────────────────────────────┘
```

| Part | Role |
|---|---|
| **`lib/dhun_engine`** | The TypeScript engine, ported one module at a time to plain C++20 + nlohmann/json. No Qt, no Logos; reusable by anything (a CLI, a test, another UI). |
| **`lib/dhun_engine/data`** | The upstream catalogues (asset templates, seed assets, GIS layers, utility bills, parameter register, …) extracted from the TypeScript literals by `scripts/extract-data.ts` and compiled into the library. One source of truth: the upstream app. |
| **`src/dhuntwin_core_impl.*`** | The universal-model impl class. Its public methods are the module API; logos-module-builder derives the LIDL contract, the Qt plugin glue and the C ABI from the header. |

### Where each upstream file went

| Upstream (`frontend/src/features/dhun/…`) | Engine |
|---|---|
| `data/constants.ts`, `data/phases.ts` | `constants.h`, `phases.{h,cpp}` |
| `lib/format.ts`, `lib/clock.ts` | `format.{h,cpp}`, `clock.{h,cpp}` |
| `engine/simulation.ts` | `simulation.{h,cpp}` |
| `engine/ledger.ts`, `scenarios.ts`, `compute.ts`, `zoneStatus.ts` | `planning.{h,cpp}` |
| `engine/computeCommonsEngine.ts`, `energy/energyEvidenceEngine.ts`, `engine/engineeringValidation.ts` | `commons.{h,cpp}` |
| `twin/buildFrame.ts`, `twin/source.ts`, `twin/useLiveTwin.ts` (frame clock) | `twin_frame.{h,cpp}` |
| `registry/AssetRepository.ts`, `telemetryEngine.ts`, `alertEngine.ts`, `testLab.ts`, `assetTemplates.ts`, `AssetRegistryContext.tsx` (service logic) | `registry.{h,cpp}` |
| `gis/spatialUtils.ts`, `gis/dhunGeoData.ts`, `gis/gisMetadata.ts`, `data/parameterRegistry.ts` | `gis.{h,cpp}` |
| `data/zones.ts`, seed and template literals, GeoJSON | `data/*.json` via `catalog.{h,cpp}` |

The pages and components (`pages/*.tsx`, `components/*`, MapLibre map, charts) belong to the UI module. Their derived values that are domain logic — the Campus issue list, the Digital Twin's frame-to-registry telemetry stream — moved into the core.

The upstream `backend/` folder (a FastAPI factory-maintenance service from an earlier project) is not used by the DhunTwin OS frontend and is not part of this port.

---

## 2. Build and run

```bash
nix build -L          # result/lib/dhuntwin_core_plugin.so (+ .dylib on macOS)
```

`flake.nix` pins **logos-module-builder 0.3.3**, the 0.3 module generation current Basecamp loads. The first build fetches the Logos SDK and Qt and takes a while; the first `nix build` also writes `flake.lock`.

Inspect and call it:

```bash
lm ./result/lib/dhuntwin_core_plugin.so
lm methods ./result/lib/dhuntwin_core_plugin.so --json
logoscore -m ./result/lib -l dhuntwin_core -c "dhuntwin_core.setPhase(buildout)"
logoscore -m ./result/lib -l dhuntwin_core -c "dhuntwin_core.campusView()"
```

The module has no dependencies. Basecamp gives each instance a data folder; the asset registry lives there as `asset_registry.json`.

---

## 3. API

Structured values cross the module boundary as **JSON text in the upstream object shapes** (`TwinSnapshot`, `Ledger`, `ScenarioResult`, `RegisteredAsset`, …), with the same field order the browser produced. Option bags are JSON too; `""` means "none". A call that cannot be served returns `{"error": "..."}`.

| Area | Methods |
|---|---|
| Module | `status()`, `catalog(name)`, `formatNumbers(requests)` |
| Phase and campus | `getPhase()`, `setPhase(phase)`, `phases()`, `twinSnapshot()`, `twinSnapshotAt(phase, epochMs)`, `campusView()`, `annualEnergy(phase)` |
| Carbon ledger | `ledger(phase, options)`, `ledgerProgramme()` |
| Scenario cockpit | `scenarioOptions()`, `defaultScenario(phase)`, `runScenario(phase, inputs)` |
| Carbon-aware compute | `computeWindows(phase)`, `computeQueue()`, `jobCarbon(phase, job, nowHour)`, `marginalIntensity(phase, hour)` |
| Compute commons | `computeCommonsDefaults()`, `calculateComputeScenario(inputs)`, `calculateCampusEnergyImpact(mwh, options)`, `calculateSolarBatteryInteraction(inputs, outputs)` |
| Energy evidence | `referenceBills()`, `aggregateBills(bills)`, `reconcileBaseline(options)`, `stackedCampusDemand(params)`, `maskAccountNumber(account)` |
| Engineering validation | `validate(kind, params)` — `energy`, `water`, `carbon`, `battery`, `compute` |
| Live digital twin | `liveTwin()`, `setTwinPaused(paused)`, `ingestGatewayMessage(raw)`, `setGatewayStatus(feed, endpoint)` |
| Asset registry | `registry()`, `getAsset(id)`, `addAsset(asset)`, `updateAsset(asset)`, `deleteAsset(id)`, `resetRegistry()`, `exportRegistry()`, `importRegistry(text)`, `assetTemplates()`, `createAssetFromTemplate(key, params)`, `recordReading(params)`, `importCsvReadings(csv, assetId)`, `getReading(assetId, channelId)`, `getHistory(assetId, channelId)`, `operationalState(assetId)`, `filterAssets(options)`, `selectAsset(id)`, `loadTestScenario(count, domain)`, `purgeTestAssets()` |
| GIS and parameters | `gisData()`, `measure(points)`, `formatCoordinates(lat, lng)`, `searchParameters(query)`, `parametersForWorkspace(workspace)` |

**Events**

| Event | When |
|---|---|
| `phaseChanged(phase)` | `setPhase` changed the phase |
| `registryChanged(reason)` | add, update, delete, import, reset, test-lab load or purge |
| `telemetryReceived(summaryJson)` | `{source, accepted}` after gateway packets, manual readings, CSV imports, or a frame streamed into the registry |

The generated contract (57 methods, 3 events) is `generated_code/dhuntwin_core.lidl` in a build; `lm methods` prints it.

---

## 4. Behaviour carried over

| Upstream behaviour | In the core |
|---|---|
| One clock for the app, IST regardless of the host timezone (`nowIST`) | Every computation takes the true instant and derives IST fields; timestamps are written `2026-10-29T21:09:05.000+05:30` |
| `TwinProvider`: phase + snapshot recomputed every 30 s | `twinSnapshot()` / `campusView()` compute at call time; the UI polls on its own cadence |
| `useLiveTwin`: 2-second frames, pause freezes the clock and the packet snapshot, packets override the model for 30 s | `liveTwin()` advances the frame clock per call unless paused; `ingestGatewayMessage` feeds the buffer; same TTL and pruning |
| Digital Twin page streams each frame's metrics into the registry | `liveTwin()` does it once per new frame |
| `LocalAssetRepository` on `localStorage`, seeded on first run | The same logic on `asset_registry.json` in the instance data folder, written atomically; in memory when no folder is given |
| Telemetry engine: 45 s stale TTL, 5-minute future-skew rejection, duplicate suppression, 60-point history | Same, with an injectable clock |
| Alert engine with hysteresis and regulatory (CERC/CPCB) classification | Same, including alert identity and detection-time persistence across evaluations |
| Test lab: deterministic synthetic assets, isolation via `isTestAsset` | Same generator and ids; benchmarks timed natively |

---

## 5. Parity with the TypeScript engine

The port is checked against the **original** engine, not against a re-statement of it:

1. `scripts/golden.ts` imports the upstream TypeScript modules directly (pinned commit `b6afcb2`) and runs them over a fixed input matrix: 64 instants × 3 phases for the campus twin, 8 ledger option sets, 17 scenario variants per phase, every compute window and job, 18 compute-commons inputs including invalid ones, all validators, 72 digital-twin frames with live and expired gateway packets, a 68-step registry script (readings, alerts with hysteresis, CSV import, add/update/delete, filters, import/export, test lab, purge) run against the real repository on a simulated `localStorage` and a pinned clock, and the GIS utilities over every GeoJSON ring.
2. The outputs are written to `tests/golden/*.json`.
3. `dhun_parity_test` replays the same inputs through `dhun_engine` and compares every field: same keys in the same order, strings byte-for-byte (formatted numbers, alert explanations, action texts), numbers to 1e-9 relative.

```
[ ok ] commons: 1074 checks
[ ok ] gis: 398 checks
[ ok ] planning: 19284 checks
[ ok ] registry: 19539 checks
[ ok ] simulation: 101228 checks
[ ok ] twin: 167182 checks
parity: all 6/6 suites match the TypeScript engine
```

The JavaScript built-ins the engine's output depends on are reproduced exactly in `js.{h,cpp}` and fuzz-checked against Node: `Math.round` (ties toward +∞, keeps −0), `Number#toString`, `Number#toFixed` (exact binary value), `Intl.NumberFormat('en-IN')` (shortest decimal, ties away from zero, lakh/crore grouping, `-0`), `JSON.stringify` number text and key order, `Date#toISOString`, and V8's `JSON.parse` error messages.

The three upstream verification suites (`computeCommons.test.ts`, `energyEvidence.test.ts`, `assetRegistry.test.ts`) are ported test-for-test and assertion-for-assertion in `tests/unit_upstream.inc`: 48 tests, 340 assertion checks, all passing.

---

## 6. Testing

| Layer | What | How |
|---|---|---|
| Unit | Constants and phases against the upstream literals, catalog completeness, JS-compatible helpers, IST clock, live-twin clock and pause, registry file persistence, the three ported upstream suites | `scripts/test-engine.sh` |
| Parity | Every engine module against the original TypeScript (above) | `scripts/test-engine.sh` |
| Goldens | Re-record from the pinned upstream commit; must not drift | `scripts/regen-golden.sh` then `git diff` |
| Module ABI | Derives the LIDL from the impl header, generates the C ABI with `logos-cpp-generator`, links it with the engine, and drives it the way `logos_host` does (dlopen, context, event callback, JSON dispatch) | `scripts/test-host.sh` |
| Module build | `nix build` on Linux and macOS | `.github/workflows/ci.yml` |

`scripts/test-engine.sh` uses Nix for its toolchain when available; `DHUN_NO_NIX=1` uses the system's cmake, compiler and nlohmann_json.

---

## 7. Differences from the web app

| Area | Difference | Why |
|---|---|---|
| Gateway WebSocket | The core parses and applies gateway messages; it does not open the socket | Core modules are Qt-free; the UI module's backend owns the connection (QtWebSockets) and calls `ingestGatewayMessage` / `setGatewayStatus`. Message format, validation and TTL are unchanged. |
| Clock fields | `summary.clock`, `frame.at`, `event.at` are the true instant with a `+05:30` offset | The browser serialised a timezone-shifted `Date` |
| Timestamps without an offset | `2026-10-29T10:00` is read as IST | The browser read it in the laptop's timezone |
| `toLowerCase` | ASCII, Latin-1, Latin Extended-A, basic Greek and Cyrillic | Every id, label and query in the data is covered |
| `formatArea` (GIS) | en-US grouping | Upstream used the browser's default locale |
| Selected asset | Kept as an id; `registry()` returns the current record | Upstream kept a possibly stale object copy |
| Benchmarks | Test-lab timings are native timings | They measure the machine running them |

---

## 8. Repository layout

```
dhuntwin-core/
├── flake.nix, metadata.json, CMakeLists.txt    Logos core module (builder 0.3.3, universal)
├── src/dhuntwin_core_impl.{h,cpp}              the module API
├── lib/dhun_engine/                            the engine (C++20, nlohmann/json)
│   ├── include/dhun/   js, constants, phases, clock, format, catalog, simulation,
│   │                   planning, commons, twin_frame, registry, gis, jsonutil
│   ├── src/
│   ├── data/           upstream catalogues (extracted), embedded at build time
│   ├── cmake/EmbedData.cmake
│   └── tests/          unit runner, ported upstream suites, parity runner
├── tests/
│   ├── golden/         outputs of the original TypeScript engine
│   └── host/           C-ABI host smoke test
├── scripts/
│   ├── extract-data.ts     upstream literals -> data/*.json
│   ├── golden.ts           upstream engine -> tests/golden/*.json
│   ├── regen-golden.sh     both, from a pinned upstream commit
│   ├── test-engine.sh      unit + parity tests
│   └── test-host.sh        module C ABI smoke test
└── .github/workflows/ci.yml
```
