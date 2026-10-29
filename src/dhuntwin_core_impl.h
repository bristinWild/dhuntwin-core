#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "logos_module_context.h"

struct DhuntwinCoreState;

/**
 * @brief DhunTwin OS core module — the twin engine behind the Basecamp app.
 *
 * Every computation of the DhunTwin OS web app (campus simulation, carbon
 * ledger, scenario cockpit, compute commons, energy evidence, live
 * digital-twin frames, asset registry with telemetry and alerts, GIS) runs
 * here, ported from the TypeScript engine and checked against it.
 *
 * Universal authoring model: these public methods ARE the module's API.
 * Structured values cross the boundary as JSON text in the upstream object
 * shapes; inputs that are option bags are JSON too. A call that cannot be
 * served returns {"error": "..."}.
 *
 * State held here, per instance: the active phase, the live-twin frame clock
 * (gateway packets, pause), and the asset registry (persisted under the
 * instance data folder as asset_registry.json — the browser's localStorage).
 */
class DhuntwinCoreImpl : public LogosModuleContext
{
public:
    DhuntwinCoreImpl();
    ~DhuntwinCoreImpl() override;

    /* ---------- module ---------- */

    /// Version, upstream reference and engine status.
    std::string status();

    /// A reference dataset by name (phases, site_plan, asset_templates, ...).
    std::string catalog(const std::string& name);

    /// Formats numbers the way the app does. requests: [{value, kind, decimals}]
    /// with kind num | percent | tonnes | rupees | signed. Returns strings.
    std::string formatNumbers(const std::string& requestsJson);

    /* ---------- phase and campus twin ---------- */

    /// The active phase: today | phase1 | buildout.
    std::string getPhase();

    /// Selects the phase every workspace shows. Emits phaseChanged.
    std::string setPhase(const std::string& phase);

    /// PHASES, PHASE_ORDER, PHASE_START_YEAR and MONTH_FACTOR.
    std::string phases();

    /// TwinSnapshot { summary, assets } for the active phase, now.
    std::string twinSnapshot();

    /// TwinSnapshot for a phase at an instant (Unix ms; 0 = now).
    std::string twinSnapshotAt(const std::string& phase, int64_t epochMs);

    /// Campus workspace: { snapshot, pressures, issues, waterTotalKlDay, landscapeKlYr }.
    std::string campusView();

    /// Whole-year totals for a phase (AnnualEnergy).
    std::string annualEnergy(const std::string& phase);

    /* ---------- carbon ledger ---------- */

    /// Ledger for a phase. options: { forestAcres, residualT, horizonYears }.
    std::string ledger(const std::string& phase, const std::string& optionsJson);

    /// Ledgers of every phase in programme order.
    std::string ledgerProgramme();

    /* ---------- scenario cockpit ---------- */

    /// COMPUTE_LOAD_KW, COMPUTE_LABEL, COOLING_PROFILE, COOLING_LABEL.
    std::string scenarioOptions();

    /// The baseline ScenarioInputs of a phase.
    std::string defaultScenario(const std::string& phase);

    /// ScenarioResult for a phase and ScenarioInputs.
    std::string runScenario(const std::string& phase, const std::string& inputsJson);

    /* ---------- carbon-aware compute ---------- */

    /// The 24 hourly windows (intensity, solar) of a phase.
    std::string computeWindows(const std::string& phase);

    /// The batch job queue.
    std::string computeQueue();

    /// Run-now vs best-window carbon of a job (ComputeJob JSON) from an hour.
    std::string jobCarbon(const std::string& phase, const std::string& jobJson, double nowHour);

    /// Marginal intensity seen on site at an hour, gCO2e/kWh.
    double marginalIntensity(const std::string& phase, double hour);

    /* ---------- compute commons ---------- */

    /// Default scenarios, campus defaults, workload profiles, evidence register.
    std::string computeCommonsDefaults();

    /// ComputeScenarioOutputs for ComputeScenarioInputs.
    std::string calculateComputeScenario(const std::string& inputsJson);

    /// Campus energy impact. options: { includeExistingCampus, includePlannedBuildings,
    /// customExistingMwh, customPlannedMwh }.
    std::string calculateCampusEnergyImpact(double computeFacilityMwhYr, const std::string& optionsJson);

    /// Solar/battery coincidence for a scenario and its outputs.
    std::string calculateSolarBatteryInteraction(const std::string& inputsJson, const std::string& outputsJson);

    /* ---------- energy evidence ---------- */

    /// The historical reference utility bills (JVVNL, Feb 2022).
    std::string referenceBills();

    /// Aggregates a bill list (null/empty text = the reference bills).
    std::string aggregateBills(const std::string& billsJson);

    /// Baseline reconciliation. options: { monthlySpend, tariff, bills }.
    std::string reconcileBaseline(const std::string& optionsJson);

    /// Stacked campus demand. params: { existingCampusMwh, plannedBuildingsMwh,
    /// includePlannedBuildings, computeFacilityMwh, computeScenarioName,
    /// effectiveTariff, gridEmissionFactorKgPerKwh }.
    std::string stackedCampusDemand(const std::string& paramsJson);

    /// "•••1234" for an account number.
    std::string maskAccountNumber(const std::string& account);

    /* ---------- engineering validation ---------- */

    /// kind: energy | water | carbon | battery | compute. Returns ValidationResult.
    std::string validate(const std::string& kind, const std::string& paramsJson);

    /* ---------- live digital twin ---------- */

    /// Advances the 2-second frame clock (unless paused) and returns
    /// { frame, events, actions, paused, feed, endpoint, lastPacketAt }.
    std::string liveTwin();

    /// Pauses or resumes the frame clock.
    bool setTwinPaused(bool paused);

    /// Feeds one gateway message (a packet, an array, or { packets: [...] }).
    /// Returns how many valid packets were accepted.
    int64_t ingestGatewayMessage(const std::string& raw);

    /// Records the gateway link state: simulated | connecting | live | stale | offline.
    std::string setGatewayStatus(const std::string& feed, const std::string& endpoint);

    /* ---------- asset registry ---------- */

    /// { assets, alerts, selectedAssetId, counts, testBenchmark }.
    std::string registry();

    /// One registered asset, or null.
    std::string getAsset(const std::string& id);

    /// Registers an asset (validated). Emits registryChanged on success.
    std::string addAsset(const std::string& assetJson);

    /// Replaces an asset record. Emits registryChanged on success.
    std::string updateAsset(const std::string& assetJson);

    /// Removes an asset. Emits registryChanged on success.
    bool deleteAsset(const std::string& id);

    /// Restores the seeded field-station inventory.
    std::string resetRegistry();

    /// The registry as pretty JSON (the download the inventory offers).
    std::string exportRegistry();

    /// Replaces the registry with an exported JSON array.
    std::string importRegistry(const std::string& text);

    /// The eight standard asset blueprints.
    std::string assetTemplates();

    /// A new asset record from a blueprint. params: { id, name, parcelId, parcelName,
    /// coordinates, mapPosition, commissioningStatus, source }.
    std::string createAssetFromTemplate(const std::string& templateKey, const std::string& paramsJson);

    /// Ingests a telemetry reading. params: { assetId, channelId, value, unit, timestamp, source }.
    std::string recordReading(const std::string& paramsJson);

    /// Ingests CSV telemetry for an asset.
    std::string importCsvReadings(const std::string& csvText, const std::string& assetId);

    /// Last reading of a channel, or null.
    std::string getReading(const std::string& assetId, const std::string& channelId);

    /// Reading history of a channel (last 60).
    std::string getHistory(const std::string& assetId, const std::string& channelId);

    /// Authoritative operational state of an asset.
    std::string operationalState(const std::string& assetId);

    /// Inventory filter. options: AssetFilterOptions.
    std::string filterAssets(const std::string& optionsJson);

    /// Selects an asset for the inspector ("" clears).
    std::string selectAsset(const std::string& id);

    /// Replaces the registry with a synthetic test-lab dataset of `count` assets.
    std::string loadTestScenario(int64_t count, const std::string& domain);

    /// Removes test-lab assets; returns how many were removed.
    int64_t purgeTestAssets();

    /* ---------- GIS and parameters ---------- */

    /// Every GIS layer and table (GeoJSON collections, centre, registry, reconciliation).
    std::string gisData();

    /// Distance and area of a measured path. points: [{lat, lng}].
    std::string measure(const std::string& pointsJson);

    /// Degrees-minutes-seconds text for a coordinate.
    std::string formatCoordinates(double lat, double lng);

    /// Parameter register entries matching a query ("" = all).
    std::string searchParameters(const std::string& query);

    /// Parameter register entries for a workspace (Campus, DigitalTwin, Ledger, Scenarios, Compute).
    std::string parametersForWorkspace(const std::string& workspace);

logos_events:
    /// The active phase changed.
    void phaseChanged(const std::string& phase);

    /// The asset registry changed (add, update, delete, import, reset, test lab).
    void registryChanged(const std::string& reason);

    /// Telemetry arrived: { source, accepted }.
    void telemetryReceived(const std::string& summaryJson);

protected:
    void onContextReady() override;

private:
    std::unique_ptr<DhuntwinCoreState> m_state;
};
