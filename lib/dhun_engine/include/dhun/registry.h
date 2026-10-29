#pragma once

/* ============================================================
   CENTRAL ASSET REGISTRY
   Ports of registry/AssetRepository.ts, telemetryEngine.ts,
   alertEngine.ts, testLab.ts, assetTemplates.ts and the service
   logic of AssetRegistryContext.tsx.

   Asset records stay JSON (RegisteredAsset, key order preserved),
   so what the registry stores, exports and hands to the UI is the
   same document the browser kept in localStorage.
   ============================================================ */

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dhun/js.h"

namespace dhun {

/* Milliseconds since epoch; injectable so tests can pin time. */
using Clock = std::function<double()>;
Clock systemClock();

struct OpResult {
    bool success = false;
    std::string error;           // empty = absent
    std::optional<int> count;

    Json toJson() const;
};

/* ---------- assetTemplates.ts ---------- */

/* [{ key, template }] in upstream declaration order */
const Json& assetTemplateCatalog();
/* ASSET_TEMPLATES[key] or nullptr */
const Json* assetTemplate(const std::string& key);
/* ALL_ASSET_TEMPLATES */
Json allAssetTemplates();

/* createAssetFromTemplate(templateOrKey, customParams). Throws
   std::invalid_argument("Asset template not found.") like upstream. */
Json createAssetFromTemplate(const Json& templateOrKey, const Json& customParams, double nowMs);

/* ---------- seed + limits ---------- */

const Json& initialSeedAssets();
const Json& regulatoryLimits();

/* ---------- persistence ---------- */

class RegistryStorage {
public:
    virtual ~RegistryStorage() = default;
    /* nullopt = nothing stored yet */
    virtual std::optional<std::string> read() = 0;
    /* false when the write failed (upstream logs and carries on) */
    virtual bool write(const std::string& text) = 0;
};

/* The localStorage stand-in: one JSON file, written atomically. */
std::unique_ptr<RegistryStorage> fileStorage(const std::string& path);
/* No storage at all: the repository keeps everything in memory
   (the upstream path when window.localStorage is unavailable). */
std::unique_ptr<RegistryStorage> memoryStorage();
/* A localStorage that lives in memory (tests, previews). */
std::unique_ptr<RegistryStorage> stringStorage();

/* ---------- AssetRepository.ts (LocalAssetRepository) ---------- */

class AssetRepository {
public:
    explicit AssetRepository(std::unique_ptr<RegistryStorage> storage);

    Json getAll() const;
    std::optional<Json> getById(const std::string& id) const;
    OpResult add(const Json& asset);
    OpResult update(const Json& asset);
    bool remove(const std::string& id);
    void resetToDefaults();
    std::string exportJson() const;
    OpResult bulkAdd(const Json& assets);
    void setAssets(const Json& assets);
    int purgeTestAssets();
    OpResult importJson(const std::string& text);

private:
    void ensureInitialized();
    void save(const Json& assets);

    std::unique_ptr<RegistryStorage> m_storage;
    std::optional<Json> m_memory;
};

/* ---------- telemetryEngine.ts ---------- */

inline constexpr double DEFAULT_STALE_THRESHOLD_SECONDS = 45;

struct IngestParams {
    std::string assetId;
    std::string channelId;
    Json value;                 // must be a finite number
    std::string unit;
    Json timestamp;             // string | number | null/absent
    std::string source;
    std::optional<std::string> notes;
};

struct IngestResult {
    bool success = false;
    std::optional<Json> reading;
    std::string error;

    Json toJson() const;
};

struct CsvImportResult {
    bool success = false;
    int importedCount = 0;
    std::vector<std::string> errors;

    Json toJson() const;
};

Json validateTelemetryReading(const Json& reading);
bool detectStaleTelemetry(const Json& timestamp, double nowMs,
                          double thresholdSeconds = DEFAULT_STALE_THRESHOLD_SECONDS);
std::string evaluateReadingQuality(double value, std::optional<double> expectedMin,
                                   std::optional<double> expectedMax,
                                   const std::optional<std::string>& timestamp, double nowMs,
                                   double staleThreshold = DEFAULT_STALE_THRESHOLD_SECONDS);
Json parseTelemetryCsv(const std::string& csvText, double nowMs);

class TelemetryEngine {
public:
    explicit TelemetryEngine(Clock clock = systemClock(),
                             double staleThresholdSeconds = DEFAULT_STALE_THRESHOLD_SECONDS);

    IngestResult ingest(const IngestParams& params);

    /* Last reading (re-graded "stale" on read when it has aged). */
    std::optional<Json> getReading(const std::string& assetId, const std::string& channelId);
    Json getHistory(const std::string& assetId, const std::string& channelId) const;

    /* planned | commissioning | active_verified | active_manual |
       simulated | stale | decommissioned */
    std::string determineOperationalState(const Json& asset);

    CsvImportResult parseCsvReadings(const std::string& csvText, const std::string& assetId);

    void clear();

private:
    struct Entry {
        std::string key;
        Json reading;
    };

    static std::string channelKey(const std::string& assetId, const std::string& channelId);
    Json* find(const std::string& key);

    Clock m_clock;
    double m_staleThresholdSeconds;
    std::vector<Entry> m_readings;                            // insertion order, like a Map
    std::vector<std::pair<std::string, Json>> m_history;      // key -> array
    std::size_t m_maxHistoryPerChannel = 60;
};

/* ---------- alertEngine.ts ---------- */

bool isAlertSuppressedByHysteresis(double value, double threshold, double deadbandRatio,
                                   bool priorAlarmActive);

class AlertEngine {
public:
    explicit AlertEngine(Clock clock = systemClock());

    /* ActiveAlert or null */
    Json evaluateChannel(const Json& asset, const Json& channel, const std::optional<Json>& reading);

    using ReadingLookup =
        std::function<std::optional<Json>(const std::string& assetId, const std::string& channelId)>;

    Json evaluateAll(const Json& assets, const ReadingLookup& getReading);

    Json getActiveAlerts() const;
    Json getAlertForAsset(const std::string& assetId) const;

private:
    void set(const std::string& key, const Json& alert);
    void erase(const std::string& key);
    const Json* get(const std::string& key) const;

    Clock m_clock;
    std::vector<std::pair<std::string, Json>> m_active;  // Map insertion order
};

/* evaluateAssetAlerts(asset, readings) — a fresh engine per call */
Json evaluateAssetAlerts(const Json& asset, const Json& readingsByChannel, double nowMs);

/* ---------- testLab.ts ---------- */

struct TestLabConfig {
    int count = 0;
    std::optional<double> includeCoordinatesPercentage;
    std::optional<double> includeAlertsPercentage;
    std::optional<std::string> datasetTag;
};

/* { assets, initialReadings, benchmark } */
Json generateTestAssets(const TestLabConfig& config, double nowMs);
Json purgeTestAssetsFromList(const Json& assets);
/* { assets, durationMs, readings } */
Json generateTestDataset(int count, double nowMs);
/* { datasetSize, filterTimeMs, searchTimeMs, sortTimeMs, estimatedMemoryKb } */
Json benchmarkRegistryPerformance(const Json& assets);

/* ---------- AssetRegistryContext.tsx: the registry service ---------- */

class AssetRegistry {
public:
    AssetRegistry(std::unique_ptr<RegistryStorage> storage, Clock clock = systemClock());

    Json assets() const { return m_assets; }
    Json alerts();

    std::optional<std::string> selectedAssetId() const { return m_selected; }
    void setSelectedAsset(const std::optional<std::string>& id) { m_selected = id; }
    Json selectedAsset() const;

    std::optional<Json> getAssetById(const std::string& id) const;
    OpResult addAsset(const Json& asset);
    OpResult updateAsset(const Json& asset);
    bool deleteAsset(const std::string& id);
    void resetDefaults();
    std::string exportJson() const;
    OpResult importJson(const std::string& text);

    IngestResult recordReading(const Json& params);
    CsvImportResult importCsvReadings(const std::string& csvText, const std::string& assetId);
    std::optional<Json> getReading(const std::string& assetId, const std::string& channelId);
    Json getHistory(const std::string& assetId, const std::string& channelId) const;
    std::string getOperationalState(const Json& asset);
    Json filterAssets(const Json& options);

    /* totalCount, activeCount, liveCount, simulatedCount, staleCount,
       commissioningCount, alertingCount, testAssetCount,
       isTestModeActive */
    Json counts();

    Json testBenchmark() const { return m_testBenchmark; }
    void loadTestScenario(int count, const std::optional<std::string>& domain);
    int purgeTestAssets();

    /* Everything the inventory views read, in one document. */
    Json snapshot();

    TelemetryEngine& telemetry() { return m_telemetry; }
    AlertEngine& alertEngine() { return m_alerts; }

private:
    void refreshAssets();

    AssetRepository m_repo;
    Clock m_clock;
    TelemetryEngine m_telemetry;
    AlertEngine m_alerts;
    Json m_assets;
    std::optional<std::string> m_selected;
    Json m_testBenchmark = nullptr;
};

}  // namespace dhun
