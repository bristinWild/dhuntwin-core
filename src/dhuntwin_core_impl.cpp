#include "dhuntwin_core_impl.h"

#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>

#include "dhun/catalog.h"
#include "dhun/commons.h"
#include "dhun/format.h"
#include "dhun/gis.h"
#include "dhun/jsonutil.h"
#include "dhun/planning.h"
#include "dhun/registry.h"
#include "dhun/simulation.h"
#include "dhun/twin_frame.h"

using dhun::Json;

/* Upstream DhunTwin OS commit this engine was ported from and is
   checked against (tests/golden was generated from it). */
static const char* const kUpstreamCommit = "b6afcb2";
static const char* const kRegistryFile = "asset_registry.json";

struct DhuntwinCoreState {
    std::mutex mutex;
    dhun::Clock clock = dhun::systemClock();
    dhun::PhaseId phase = dhun::PhaseId::Today;
    std::unique_ptr<dhun::AssetRegistry> registry;
    dhun::LiveTwin live{dhun::wallClockMs()};
    std::string registryPath;

    /* the frame last streamed into the registry (DigitalTwin page effect) */
    std::optional<double> streamedFrameAt;
    std::optional<dhun::PhaseId> streamedPhase;

    DhuntwinCoreState() : registry(std::make_unique<dhun::AssetRegistry>(dhun::memoryStorage(), clock)) {}
};

namespace {

using Lock = std::lock_guard<std::mutex>;

std::string errorJson(const std::string& message)
{
    Json j;
    j["error"] = message;
    return dhun::js::stringify(j);
}

/* Runs a call; any failure becomes {"error": "..."} instead of an
   exception crossing the module boundary. */
template <typename F>
std::string guarded(F&& fn)
{
    try {
        return fn();
    } catch (const std::exception& e) {
        return errorJson(e.what());
    } catch (...) {
        return errorJson("unexpected failure");
    }
}

std::string out(const Json& value)
{
    return dhun::js::stringify(value);
}

/* JSON text input; "" and whitespace mean `fallback`. */
Json input(const std::string& text, Json fallback = Json::object())
{
    if (dhun::js::trim(text).empty()) return fallback;
    return dhun::js::parse(text);
}

dhun::PhaseId phaseOf(const std::string& key)
{
    const auto phase = dhun::parsePhase(key);
    if (!phase) throw std::invalid_argument("unknown phase '" + key + "' (today | phase1 | buildout)");
    return *phase;
}

Json opt(const std::optional<Json>& value)
{
    return value ? *value : Json(nullptr);
}

}  // namespace

DhuntwinCoreImpl::DhuntwinCoreImpl() : m_state(std::make_unique<DhuntwinCoreState>()) {}

DhuntwinCoreImpl::~DhuntwinCoreImpl() = default;

void DhuntwinCoreImpl::onContextReady()
{
    /* The registry persists where the browser kept localStorage: the
       host-owned data folder of this instance. Without one (a bare
       test host) it stays in memory. */
    const std::string& dir = instancePersistencePath();
    if (dir.empty()) return;

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    Lock lock(m_state->mutex);
    m_state->registryPath = (std::filesystem::path(dir) / kRegistryFile).string();
    m_state->registry =
        std::make_unique<dhun::AssetRegistry>(dhun::fileStorage(m_state->registryPath), m_state->clock);
}

/* ============================================================
   MODULE
   ============================================================ */

std::string DhuntwinCoreImpl::status()
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        Json j;
        j["module"] = "dhuntwin_core";
        j["version"] = "0.1.0";
        j["upstream"] = std::string("tuhin-codes/DhunTwin-OS@") + kUpstreamCommit;
        j["phase"] = std::string(dhun::phaseKey(m_state->phase));
        j["registryPath"] = m_state->registryPath.empty() ? Json(nullptr) : Json(m_state->registryPath);
        j["assets"] = m_state->registry->assets().size();
        j["datasets"] = dhun::catalog::names();
        return out(j);
    });
}

std::string DhuntwinCoreImpl::catalog(const std::string& name)
{
    return guarded([&] { return std::string(dhun::catalog::raw(name)); });
}

std::string DhuntwinCoreImpl::formatNumbers(const std::string& requestsJson)
{
    return guarded([&] {
        const Json requests = input(requestsJson, Json::array());
        Json results = Json::array();
        for (const auto& r : requests) {
            const double value = dhun::numberOf(r, "value");
            const int decimals = static_cast<int>(dhun::numberOr(r, "decimals", 0));
            const std::string kind = dhun::stringOr(r, "kind", "num");
            if (kind == "percent") results.push_back(dhun::percent(value, decimals));
            else if (kind == "tonnes") results.push_back(dhun::tonnes(value));
            else if (kind == "rupees") results.push_back(dhun::rupees(value));
            else if (kind == "signed") results.push_back(dhun::signedNum(value, decimals));
            else if (kind == "fixed") results.push_back(dhun::js::toFixed(value, decimals));
            else results.push_back(dhun::num(value, decimals));
        }
        return out(results);
    });
}

/* ============================================================
   PHASE AND CAMPUS TWIN
   ============================================================ */

std::string DhuntwinCoreImpl::getPhase()
{
    Lock lock(m_state->mutex);
    return std::string(dhun::phaseKey(m_state->phase));
}

std::string DhuntwinCoreImpl::setPhase(const std::string& phase)
{
    std::string result;
    bool changed = false;
    {
        Lock lock(m_state->mutex);
        const auto parsed = dhun::parsePhase(phase);
        if (!parsed) return errorJson("unknown phase '" + phase + "' (today | phase1 | buildout)");
        changed = *parsed != m_state->phase;
        m_state->phase = *parsed;
        result = std::string(dhun::phaseKey(m_state->phase));
    }
    if (changed) phaseChanged(result);
    return result;
}

std::string DhuntwinCoreImpl::phases()
{
    return guarded([&] { return std::string(dhun::catalog::raw("phases")); });
}

std::string DhuntwinCoreImpl::twinSnapshot()
{
    return guarded([&] {
        dhun::PhaseId phase;
        {
            Lock lock(m_state->mutex);
            phase = m_state->phase;
        }
        return out(dhun::toJson(dhun::buildTwin(phase, dhun::SiteTime::fromEpochMs(m_state->clock()))));
    });
}

std::string DhuntwinCoreImpl::twinSnapshotAt(const std::string& phase, int64_t epochMs)
{
    return guarded([&] {
        const double at = epochMs == 0 ? m_state->clock() : static_cast<double>(epochMs);
        return out(dhun::toJson(dhun::buildTwin(phaseOf(phase), dhun::SiteTime::fromEpochMs(at))));
    });
}

std::string DhuntwinCoreImpl::campusView()
{
    return guarded([&] {
        dhun::PhaseId phase;
        {
            Lock lock(m_state->mutex);
            phase = m_state->phase;
        }
        const auto twin = dhun::buildTwin(phase, dhun::SiteTime::fromEpochMs(m_state->clock()));
        const double landscape = dhun::landscapeWaterKlYr(phase);

        Json j;
        j["snapshot"] = dhun::toJson(twin);
        j["pressures"] = dhun::toJson(dhun::zonePressures(twin.summary, twin.assets));
        j["issues"] = dhun::zoneIssues(twin.summary, twin.assets);
        j["waterTotalKlDay"] = twin.summary.waterKlDay + landscape / 365;
        j["landscapeKlYr"] = landscape;
        return out(j);
    });
}

std::string DhuntwinCoreImpl::annualEnergy(const std::string& phase)
{
    return guarded([&] { return out(dhun::toJson(dhun::annualEnergy(phaseOf(phase)))); });
}

/* ============================================================
   LEDGER, SCENARIOS, COMPUTE
   ============================================================ */

std::string DhuntwinCoreImpl::ledger(const std::string& phase, const std::string& optionsJson)
{
    return guarded([&] { return out(dhun::buildLedger(phaseOf(phase), input(optionsJson))); });
}

std::string DhuntwinCoreImpl::ledgerProgramme()
{
    return guarded([&] {
        Json list = Json::array();
        for (dhun::PhaseId id : dhun::PHASE_ORDER) {
            Json item;
            item["id"] = std::string(dhun::phaseKey(id));
            item["ledger"] = dhun::buildLedger(id);
            list.push_back(item);
        }
        return out(list);
    });
}

std::string DhuntwinCoreImpl::scenarioOptions()
{
    return guarded([&] { return std::string(dhun::catalog::raw("scenario_options")); });
}

std::string DhuntwinCoreImpl::defaultScenario(const std::string& phase)
{
    return guarded([&] { return out(dhun::defaultScenario(phaseOf(phase))); });
}

std::string DhuntwinCoreImpl::runScenario(const std::string& phase, const std::string& inputsJson)
{
    return guarded([&] { return out(dhun::runScenario(phaseOf(phase), input(inputsJson))); });
}

std::string DhuntwinCoreImpl::computeWindows(const std::string& phase)
{
    return guarded([&] { return out(dhun::computeWindows(phaseOf(phase))); });
}

std::string DhuntwinCoreImpl::computeQueue()
{
    return guarded([&] { return out(dhun::computeQueue()); });
}

std::string DhuntwinCoreImpl::jobCarbon(const std::string& phase, const std::string& jobJson, double nowHour)
{
    return guarded([&] { return out(dhun::jobCarbon(phaseOf(phase), input(jobJson), nowHour)); });
}

double DhuntwinCoreImpl::marginalIntensity(const std::string& phase, double hour)
{
    const auto parsed = dhun::parsePhase(phase);
    if (!parsed) return std::numeric_limits<double>::quiet_NaN();
    return dhun::marginalIntensity(*parsed, hour);
}

std::string DhuntwinCoreImpl::computeCommonsDefaults()
{
    return guarded([&] { return std::string(dhun::catalog::raw("compute_commons")); });
}

std::string DhuntwinCoreImpl::calculateComputeScenario(const std::string& inputsJson)
{
    return guarded([&] { return out(dhun::calculateComputeScenario(input(inputsJson))); });
}

std::string DhuntwinCoreImpl::calculateCampusEnergyImpact(double computeFacilityMwhYr, const std::string& optionsJson)
{
    return guarded([&] {
        const Json o = input(optionsJson);
        auto flag = [&](const char* key) {
            return !(o.contains(key) && o[key].is_boolean()) || o[key].get<bool>();
        };
        auto custom = [&](const char* key) -> std::optional<double> {
            if (o.contains(key) && o[key].is_number()) return o[key].get<double>();
            return std::nullopt;
        };
        return out(dhun::calculateCampusEnergyImpact(computeFacilityMwhYr, flag("includeExistingCampus"),
                                                     flag("includePlannedBuildings"), custom("customExistingMwh"),
                                                     custom("customPlannedMwh")));
    });
}

std::string DhuntwinCoreImpl::calculateSolarBatteryInteraction(const std::string& inputsJson,
                                                               const std::string& outputsJson)
{
    return guarded([&] {
        return out(dhun::calculateSolarBatteryInteraction(input(inputsJson), input(outputsJson)));
    });
}

/* ============================================================
   ENERGY EVIDENCE AND VALIDATION
   ============================================================ */

std::string DhuntwinCoreImpl::referenceBills()
{
    return guarded([&] { return std::string(dhun::catalog::raw("utility_bills")); });
}

std::string DhuntwinCoreImpl::aggregateBills(const std::string& billsJson)
{
    return guarded([&] {
        const Json bills = input(billsJson, nullptr);
        return out(dhun::calculateAggregateBills(bills.is_array() ? bills : dhun::historicalReferenceBills()));
    });
}

std::string DhuntwinCoreImpl::reconcileBaseline(const std::string& optionsJson)
{
    return guarded([&] {
        const Json o = input(optionsJson);
        const Json* bills = o.contains("bills") && o["bills"].is_array() ? &o["bills"] : nullptr;
        return out(dhun::reconcileBaselineWithEvidence(dhun::numberOr(o, "monthlySpend", 250000),
                                                       dhun::numberOr(o, "tariff", 10.0), bills));
    });
}

std::string DhuntwinCoreImpl::stackedCampusDemand(const std::string& paramsJson)
{
    return guarded([&] {
        const Json p = input(paramsJson);
        return out(dhun::calculateStackedCampusDemand(
            dhun::numberOf(p, "existingCampusMwh"), dhun::numberOf(p, "plannedBuildingsMwh"),
            dhun::truthyKey(p, "includePlannedBuildings"), dhun::numberOf(p, "computeFacilityMwh"),
            dhun::stringOr(p, "computeScenarioName"), dhun::numberOr(p, "effectiveTariff", 10.0),
            dhun::numberOr(p, "gridEmissionFactorKgPerKwh", 0.71)));
    });
}

std::string DhuntwinCoreImpl::maskAccountNumber(const std::string& account)
{
    return dhun::maskAccountNumber(account);
}

std::string DhuntwinCoreImpl::validate(const std::string& kind, const std::string& paramsJson)
{
    return guarded([&] {
        const Json p = input(paramsJson);
        if (kind == "energy") return out(dhun::validateEnergyBalance(p));
        if (kind == "water") return out(dhun::validateWaterBalance(p));
        if (kind == "carbon") return out(dhun::validateCarbonBoundaries(p));
        if (kind == "battery") return out(dhun::validateBatteryDispatch(p));
        if (kind == "compute") return out(dhun::validateComputeMonitoring(p));
        throw std::invalid_argument("unknown validation '" + kind + "' (energy | water | carbon | battery | compute)");
    });
}

/* ============================================================
   LIVE DIGITAL TWIN
   ============================================================ */

std::string DhuntwinCoreImpl::liveTwin()
{
    int streamed = 0;
    const std::string result = guarded([&] {
        Lock lock(m_state->mutex);
        auto& s = *m_state;
        const double now = s.clock();

        s.live.tick(now);
        const dhun::TwinFrame frame = s.live.frame(s.phase, now);

        /* The Digital Twin workspace streams every frame into the
           registry's telemetry engine, so inventory views see the same
           readings. A new frame is a new clock tick or a new phase. */
        if (!s.streamedFrameAt || *s.streamedFrameAt != frame.at.epochMs || s.streamedPhase != s.phase) {
            for (const auto& node : frame.nodes) {
                for (const auto& m : node.metrics) {
                    Json reading;
                    reading["assetId"] = node.id;
                    reading["channelId"] = node.id + "-" + m.key;
                    reading["value"] = m.value;
                    reading["unit"] = m.unit;
                    reading["source"] = m.live ? "gateway" : "simulation";
                    s.registry->recordReading(reading);
                    ++streamed;
                }
            }
            s.streamedFrameAt = frame.at.epochMs;
            s.streamedPhase = s.phase;
        }

        Json j;
        j["frame"] = dhun::toJson(frame);
        j["events"] = dhun::buildEvents(frame);
        j["actions"] = dhun::buildActions(s.phase, frame.at);
        j["paused"] = s.live.paused();
        j["feed"] = s.live.feed();
        j["endpoint"] = s.live.endpoint().empty() ? Json(nullptr) : Json(s.live.endpoint());
        j["lastPacketAt"] = s.live.lastPacketAt() ? Json(*s.live.lastPacketAt()) : Json(nullptr);
        return out(j);
    });

    if (streamed > 0) {
        Json summary;
        summary["source"] = "twin-frame";
        summary["accepted"] = streamed;
        telemetryReceived(out(summary));
    }
    return result;
}

bool DhuntwinCoreImpl::setTwinPaused(bool paused)
{
    Lock lock(m_state->mutex);
    m_state->live.setPaused(paused);
    return m_state->live.paused();
}

int64_t DhuntwinCoreImpl::ingestGatewayMessage(const std::string& raw)
{
    std::size_t accepted = 0;
    {
        Lock lock(m_state->mutex);
        const auto packets = dhun::parsePacket(raw, dhun::wallClockMs());
        for (const auto& packet : packets) m_state->live.receive(packet);
        accepted = packets.size();
    }
    if (accepted > 0) {
        Json summary;
        summary["source"] = "gateway";
        summary["accepted"] = accepted;
        telemetryReceived(out(summary));
    }
    return static_cast<int64_t>(accepted);
}

std::string DhuntwinCoreImpl::setGatewayStatus(const std::string& feed, const std::string& endpoint)
{
    static const char* const kFeeds[] = {"simulated", "connecting", "live", "stale", "offline"};
    for (const char* f : kFeeds) {
        if (feed == f) {
            Lock lock(m_state->mutex);
            m_state->live.setFeed(feed);
            m_state->live.setEndpoint(endpoint);
            return feed;
        }
    }
    return errorJson("unknown feed status '" + feed + "'");
}

/* ============================================================
   ASSET REGISTRY
   ============================================================ */

std::string DhuntwinCoreImpl::registry()
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        return out(m_state->registry->snapshot());
    });
}

std::string DhuntwinCoreImpl::getAsset(const std::string& id)
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        return out(opt(m_state->registry->getAssetById(id)));
    });
}

std::string DhuntwinCoreImpl::addAsset(const std::string& assetJson)
{
    bool changed = false;
    const std::string result = guarded([&] {
        const Json asset = input(assetJson);
        Lock lock(m_state->mutex);
        const auto res = m_state->registry->addAsset(asset);
        changed = res.success;
        return out(res.toJson());
    });
    if (changed) registryChanged("add");
    return result;
}

std::string DhuntwinCoreImpl::updateAsset(const std::string& assetJson)
{
    bool changed = false;
    const std::string result = guarded([&] {
        const Json asset = input(assetJson);
        Lock lock(m_state->mutex);
        const auto res = m_state->registry->updateAsset(asset);
        changed = res.success;
        return out(res.toJson());
    });
    if (changed) registryChanged("update");
    return result;
}

bool DhuntwinCoreImpl::deleteAsset(const std::string& id)
{
    bool removed = false;
    {
        Lock lock(m_state->mutex);
        removed = m_state->registry->deleteAsset(id);
    }
    if (removed) registryChanged("delete");
    return removed;
}

std::string DhuntwinCoreImpl::resetRegistry()
{
    const std::string result = guarded([&] {
        Lock lock(m_state->mutex);
        m_state->registry->resetDefaults();
        return out(m_state->registry->snapshot());
    });
    registryChanged("reset");
    return result;
}

std::string DhuntwinCoreImpl::exportRegistry()
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        return m_state->registry->exportJson();
    });
}

std::string DhuntwinCoreImpl::importRegistry(const std::string& text)
{
    bool changed = false;
    const std::string result = guarded([&] {
        Lock lock(m_state->mutex);
        const auto res = m_state->registry->importJson(text);
        changed = res.success;
        return out(res.toJson());
    });
    if (changed) registryChanged("import");
    return result;
}

std::string DhuntwinCoreImpl::assetTemplates()
{
    return guarded([&] { return std::string(dhun::catalog::raw("asset_templates")); });
}

std::string DhuntwinCoreImpl::createAssetFromTemplate(const std::string& templateKey, const std::string& paramsJson)
{
    return guarded([&] {
        return out(dhun::createAssetFromTemplate(Json(templateKey), input(paramsJson), m_state->clock()));
    });
}

std::string DhuntwinCoreImpl::recordReading(const std::string& paramsJson)
{
    bool accepted = false;
    const std::string result = guarded([&] {
        const Json params = input(paramsJson);
        Lock lock(m_state->mutex);
        const auto res = m_state->registry->recordReading(params);
        accepted = res.success;
        return out(res.toJson());
    });
    if (accepted) {
        Json summary;
        summary["source"] = "manual";
        summary["accepted"] = 1;
        telemetryReceived(out(summary));
    }
    return result;
}

std::string DhuntwinCoreImpl::importCsvReadings(const std::string& csvText, const std::string& assetId)
{
    int imported = 0;
    const std::string result = guarded([&] {
        Lock lock(m_state->mutex);
        const auto res = m_state->registry->importCsvReadings(csvText, assetId);
        imported = res.importedCount;
        return out(res.toJson());
    });
    if (imported > 0) {
        Json summary;
        summary["source"] = "csv";
        summary["accepted"] = imported;
        telemetryReceived(out(summary));
    }
    return result;
}

std::string DhuntwinCoreImpl::getReading(const std::string& assetId, const std::string& channelId)
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        return out(opt(m_state->registry->getReading(assetId, channelId)));
    });
}

std::string DhuntwinCoreImpl::getHistory(const std::string& assetId, const std::string& channelId)
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        return out(m_state->registry->getHistory(assetId, channelId));
    });
}

std::string DhuntwinCoreImpl::operationalState(const std::string& assetId)
{
    return guarded([&] {
        Lock lock(m_state->mutex);
        const auto asset = m_state->registry->getAssetById(assetId);
        if (!asset) throw std::invalid_argument("asset '" + assetId + "' not found");
        return m_state->registry->getOperationalState(*asset);
    });
}

std::string DhuntwinCoreImpl::filterAssets(const std::string& optionsJson)
{
    return guarded([&] {
        const Json options = input(optionsJson);
        Lock lock(m_state->mutex);
        return out(m_state->registry->filterAssets(options));
    });
}

std::string DhuntwinCoreImpl::selectAsset(const std::string& id)
{
    Lock lock(m_state->mutex);
    m_state->registry->setSelectedAsset(id.empty() ? std::nullopt : std::optional<std::string>(id));
    return out(m_state->registry->selectedAsset());
}

std::string DhuntwinCoreImpl::loadTestScenario(int64_t count, const std::string& domain)
{
    const std::string result = guarded([&] {
        if (count < 0 || count > 100000) throw std::invalid_argument("count must be between 0 and 100000");
        Lock lock(m_state->mutex);
        m_state->registry->loadTestScenario(static_cast<int>(count),
                                            domain.empty() ? std::nullopt : std::optional<std::string>(domain));
        return out(m_state->registry->testBenchmark());
    });
    registryChanged("test-lab");
    return result;
}

int64_t DhuntwinCoreImpl::purgeTestAssets()
{
    int removed = 0;
    {
        Lock lock(m_state->mutex);
        removed = m_state->registry->purgeTestAssets();
    }
    registryChanged("purge");
    return removed;
}

/* ============================================================
   GIS AND PARAMETERS
   ============================================================ */

std::string DhuntwinCoreImpl::gisData()
{
    return guarded([&] { return std::string(dhun::catalog::raw("gis")); });
}

std::string DhuntwinCoreImpl::measure(const std::string& pointsJson)
{
    return guarded([&] {
        const auto points = dhun::geoPoints(input(pointsJson, Json::array()));
        const double distance = dhun::calculatePathDistanceMeters(points);
        const double area = dhun::calculateGeodesicAreaSqm(points);
        Json j;
        j["distanceMeters"] = distance;
        j["distanceText"] = dhun::formatDistance(distance);
        j["areaSqm"] = area;
        j["area"] = dhun::formatArea(area);
        return out(j);
    });
}

std::string DhuntwinCoreImpl::formatCoordinates(double lat, double lng)
{
    return dhun::formatCoordinatesDMS(lat, lng);
}

std::string DhuntwinCoreImpl::searchParameters(const std::string& query)
{
    return guarded([&] { return out(dhun::searchParameterRegistry(query)); });
}

std::string DhuntwinCoreImpl::parametersForWorkspace(const std::string& workspace)
{
    return guarded([&] { return out(dhun::getParametersByWorkspace(workspace)); });
}
