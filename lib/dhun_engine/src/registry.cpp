#include "dhun/registry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include "dhun/catalog.h"
#include "dhun/clock.h"
#include "dhun/jsonutil.h"

namespace dhun {

namespace {

const Json kNull = nullptr;

const Json& field(const Json& obj, const char* key)
{
    if (!obj.is_object()) return kNull;
    auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

bool has(const Json& obj, const char* key)
{
    return obj.is_object() && obj.contains(key);
}

/* `${value}` */
std::string str(const Json& v)
{
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number()) return js::toString(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_null()) return "null";
    return js::stringify(v);
}

std::string lowerId(const Json& asset)
{
    const Json& id = field(asset, "id");
    return id.is_string() ? js::toLower(id.get<std::string>()) : js::toLower(str(id));
}

std::string idOf(const Json& asset)
{
    const Json& id = field(asset, "id");
    return id.is_string() ? id.get<std::string>() : str(id);
}

std::string stringField(const Json& obj, const char* key)
{
    const Json& v = field(obj, key);
    return v.is_string() ? v.get<std::string>() : std::string();
}

const Json& channelsOf(const Json& asset)
{
    static const Json empty = Json::array();
    const Json& c = field(asset, "channels");
    return c.is_array() ? c : empty;
}

/* Number#toString(36) for a non-negative integer */
std::string base36(double value)
{
    auto n = static_cast<uint64_t>(value);
    if (n == 0) return "0";
    std::string out;
    while (n > 0) {
        const int d = static_cast<int>(n % 36);
        out.insert(out.begin(), static_cast<char>(d < 10 ? '0' + d : 'a' + d - 10));
        n /= 36;
    }
    return out;
}

/* str.trim().toLowerCase().replace(/\s+/g, "-") */
std::string normalizeId(const std::string& id)
{
    const std::string text = js::toLower(js::trim(id));
    std::string out;
    bool inSpace = false;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        std::size_t len = 1;
        if (!space && c == 0xC2 && i + 1 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            space = true;
            len = 2;
        } else if (!space && c == 0xE2 && i + 2 < text.size()) {
            const auto b1 = static_cast<unsigned char>(text[i + 1]);
            const auto b2 = static_cast<unsigned char>(text[i + 2]);
            if ((b1 == 0x80 && (b2 <= 0x8A || b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) ||
                (b1 == 0x81 && b2 == 0x9F)) {
                space = true;
                len = 3;
            }
        } else if (!space && c == 0xE3 && i + 2 < text.size() &&
                   static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                   static_cast<unsigned char>(text[i + 2]) == 0x80) {
            space = true;
            len = 3;
        } else if (!space && c == 0xEF && i + 2 < text.size() &&
                   static_cast<unsigned char>(text[i + 1]) == 0xBB &&
                   static_cast<unsigned char>(text[i + 2]) == 0xBF) {
            space = true;
            len = 3;
        }
        if (space) {
            if (!inSpace) out.push_back('-');
            inSpace = true;
        } else {
            out.append(text, i, len);
            inSpace = false;
        }
        i += len;
    }
    return out;
}

double steadyMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

double round2(double ms)
{
    return js::round(ms * 100) / 100;
}

std::vector<std::string> splitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(text[i]);
        }
    }
    lines.push_back(current);
    return lines;
}

std::vector<std::string> splitComma(const std::string& line)
{
    std::vector<std::string> out;
    std::string current;
    for (char c : line) {
        if (c == ',') {
            out.push_back(js::trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    out.push_back(js::trim(current));
    return out;
}

int findHeader(const std::vector<std::string>& headers, std::initializer_list<const char*> names)
{
    for (std::size_t i = 0; i < headers.size(); ++i) {
        for (const char* n : names) {
            if (headers[i] == n) return static_cast<int>(i);
        }
    }
    return -1;
}

const std::string* col(const std::vector<std::string>& cols, int idx)
{
    if (idx < 0 || static_cast<std::size_t>(idx) >= cols.size()) return nullptr;
    return &cols[static_cast<std::size_t>(idx)];
}

}  // namespace

Clock systemClock()
{
    return [] { return wallClockMs(); };
}

Json OpResult::toJson() const
{
    Json j;
    j["success"] = success;
    if (!error.empty()) j["error"] = error;
    if (count) j["count"] = *count;
    return j;
}

/* ============================================================
   TEMPLATES  (registry/assetTemplates.ts)
   ============================================================ */

const Json& assetTemplateCatalog()
{
    return catalog::get("asset_templates");
}

const Json* assetTemplate(const std::string& key)
{
    for (const auto& entry : assetTemplateCatalog()) {
        if (entry["key"] == key) return &entry["template"];
    }
    return nullptr;
}

Json allAssetTemplates()
{
    Json out = Json::array();
    for (const auto& entry : assetTemplateCatalog()) out.push_back(entry["template"]);
    return out;
}

Json createAssetFromTemplate(const Json& templateOrKey, const Json& p, double nowMs)
{
    const Json* tpl = nullptr;
    if (templateOrKey.is_string()) {
        tpl = assetTemplate(templateOrKey.get<std::string>());
    } else if (templateOrKey.is_object()) {
        tpl = &templateOrKey;
    }
    if (!tpl) throw std::invalid_argument("Asset template not found.");
    const Json& t = *tpl;

    const Json& id = field(p, "id");

    Json channels = Json::array();
    for (const auto& ch : channelsOf(t)) {
        Json c = ch;
        c["assetId"] = id;
        c["source"] = truthyKey(p, "source") ? p["source"] : field(ch, "source");
        channels.push_back(c);
    }

    const Json& coordinates = field(p, "coordinates");
    const Json& mapPosition = field(p, "mapPosition");
    const bool mapPositive = mapPosition.is_object() && js::toNumber(field(mapPosition, "x")) > 0 &&
                             js::toNumber(field(mapPosition, "y")) > 0;
    const bool hasCoords = truthy(coordinates) || (truthy(mapPosition) && mapPositive);

    const std::string status = truthyKey(p, "commissioningStatus") ? str(p["commissioningStatus"]) : "Active";

    Json asset;
    asset["id"] = id;
    asset["assetId"] = id;
    asset["name"] = field(p, "name");
    asset["type"] = field(t, "type");
    asset["assetType"] = field(t, "type");
    asset["subtype"] = field(t, "subtype");
    asset["assetSubtype"] = field(t, "subtype");
    asset["domain"] = field(t, "domain");
    asset["description"] = field(t, "description");
    asset["commissioningStatus"] = status;
    asset["lifecycleStatus"] = status;
    /* compared against the raw parameter: an omitted status defaults
       to "Active" above but still reads as disconnected here */
    const bool activeParam = field(p, "commissioningStatus") == "Active";
    asset["connectivityStatus"] = activeParam ? "Connected" : "Disconnected";
    asset["monitoringMode"] = activeParam ? "Simulated" : "Uncommissioned";

    Json location;
    location["parcelId"] = truthyKey(p, "parcelId") ? p["parcelId"] : Json("p1");
    location["parcelName"] = truthyKey(p, "parcelName")       ? p["parcelName"]
                             : truthyKey(p, "locationParcel") ? p["locationParcel"]
                                                              : Json("Campus Center");
    if (truthy(coordinates) && coordinates.is_object() && coordinates.contains("lat")) {
        location["coordinates"] = coordinates;
    }
    if (truthy(mapPosition)) {
        location["mapPosition"] = mapPosition;
    } else if (truthy(coordinates) && coordinates.is_object() && coordinates.contains("x")) {
        location["mapPosition"] = coordinates;
    }
    location["hasVerifiedCoordinates"] = hasCoords;
    asset["location"] = location;

    asset["ratedCapacity"] = field(t, "defaultCapacity");
    asset["capacityUnit"] = field(t, "capacityUnit");
    asset["channels"] = channels;
    if (!channels.empty() && has(channels[0], "id")) asset["primaryChannelId"] = channels[0]["id"];
    asset["evidenceClassification"] = "Calculated";
    asset["verificationStatus"] = "verified";
    if (has(t, "operatingRange")) asset["operatingRange"] = t["operatingRange"];
    if (has(t, "specifications")) asset["specifications"] = t["specifications"];
    asset["createdAt"] = js::toISOString(nowMs);
    asset["updatedAt"] = js::toISOString(nowMs);
    return asset;
}

const Json& initialSeedAssets()
{
    return catalog::get("seed_assets");
}

const Json& regulatoryLimits()
{
    return catalog::get("regulatory_limits");
}

/* ============================================================
   PERSISTENCE
   ============================================================ */

namespace {

class FileStorage final : public RegistryStorage {
public:
    explicit FileStorage(std::string path) : m_path(std::move(path)) {}

    std::optional<std::string> read() override
    {
        std::ifstream in(m_path, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool write(const std::string& text) override
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path target(m_path);
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

        const fs::path tmp = target.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return false;
            out << text;
            if (!out) return false;
        }
        fs::rename(tmp, target, ec);
        if (ec) {
            fs::remove(tmp, ec);
            return false;
        }
        return true;
    }

private:
    std::string m_path;
};

}  // namespace

std::unique_ptr<RegistryStorage> fileStorage(const std::string& path)
{
    return std::make_unique<FileStorage>(path);
}

std::unique_ptr<RegistryStorage> memoryStorage()
{
    return nullptr;
}

namespace {

class StringStorage final : public RegistryStorage {
public:
    std::optional<std::string> read() override { return m_text; }
    bool write(const std::string& text) override
    {
        m_text = text;
        return true;
    }

private:
    std::optional<std::string> m_text;
};

}  // namespace

std::unique_ptr<RegistryStorage> stringStorage()
{
    return std::make_unique<StringStorage>();
}

/* ============================================================
   REPOSITORY  (registry/AssetRepository.ts)
   ============================================================ */

AssetRepository::AssetRepository(std::unique_ptr<RegistryStorage> storage) : m_storage(std::move(storage))
{
    ensureInitialized();
}

void AssetRepository::ensureInitialized()
{
    if (!m_storage) {
        if (!m_memory) m_memory = initialSeedAssets();
        return;
    }
    if (!m_storage->read()) {
        if (!m_storage->write(js::stringify(initialSeedAssets()))) {
            if (!m_memory) m_memory = initialSeedAssets();
        }
    }
}

Json AssetRepository::getAll() const
{
    if (!m_storage) return m_memory ? *m_memory : initialSeedAssets();

    const auto raw = m_storage->read();
    if (!raw || raw->empty()) return m_memory ? *m_memory : initialSeedAssets();

    try {
        Json parsed = js::parse(*raw);
        if (parsed.is_array() && !parsed.empty()) return parsed;
        return initialSeedAssets();
    } catch (const std::exception&) {
        return m_memory ? *m_memory : initialSeedAssets();
    }
}

std::optional<Json> AssetRepository::getById(const std::string& id) const
{
    const std::string wanted = js::toLower(id);
    for (const auto& item : getAll()) {
        if (lowerId(item) == wanted) return item;
    }
    return std::nullopt;
}

OpResult AssetRepository::add(const Json& asset)
{
    if (!truthyKey(asset, "id") || !truthyKey(asset, "name") || !truthyKey(asset, "domain") ||
        !truthyKey(asset, "type")) {
        return {false, "Missing required fields: ID, Name, Domain, and Type are mandatory.", std::nullopt};
    }

    const std::string normalizedId = normalizeId(idOf(asset));
    Json all = getAll();
    for (const auto& item : all) {
        if (lowerId(item) == normalizedId) {
            return {false, "Asset with ID '" + idOf(asset) + "' already exists in the registry.", std::nullopt};
        }
    }

    const Json& capacity = field(asset, "ratedCapacity");
    if (!capacity.is_number() || std::isnan(capacity.get<double>()) || capacity.get<double>() < 0) {
        return {false, "Rated capacity must be a non-negative number.", std::nullopt};
    }

    const Json& unit = field(asset, "capacityUnit");
    if (!truthy(unit) || !unit.is_string() || js::trim(unit.get<std::string>()).empty()) {
        return {false, "Capacity unit is required.", std::nullopt};
    }

    Json cleaned = asset;
    cleaned["id"] = normalizedId;
    cleaned["name"] = js::trim(str(asset["name"]));
    cleaned["type"] = js::trim(str(asset["type"]));
    {
        const Json& description = field(asset, "description");
        const std::string trimmed = description.is_string() ? js::trim(description.get<std::string>()) : "";
        cleaned["description"] = trimmed.empty() ? "No description provided." : trimmed;
    }
    {
        const Json& loc = field(asset, "location");
        Json location = loc.is_object() ? loc : Json::object();
        const Json& coords = field(loc, "coordinates");
        const Json& map = field(loc, "mapPosition");
        const bool verified = truthy(coords) || (truthy(field(map, "x")) && truthy(field(map, "y")));
        location["hasVerifiedCoordinates"] = verified;
        cleaned["location"] = location;
    }

    Json updated = Json::array();
    updated.push_back(cleaned);
    for (auto& item : all) updated.push_back(std::move(item));
    save(updated);
    return {true, "", std::nullopt};
}

OpResult AssetRepository::update(const Json& asset)
{
    if (!truthyKey(asset, "id")) return {false, "Asset ID is required for update.", std::nullopt};

    Json all = getAll();
    const std::string wanted = lowerId(asset);
    for (auto& item : all) {
        if (lowerId(item) == wanted) {
            item = asset;
            save(all);
            return {true, "", std::nullopt};
        }
    }
    return {false, "Asset '" + idOf(asset) + "' not found in registry.", std::nullopt};
}

bool AssetRepository::remove(const std::string& id)
{
    const Json all = getAll();
    const std::string wanted = js::toLower(id);
    Json filtered = Json::array();
    for (const auto& item : all) {
        if (lowerId(item) != wanted) filtered.push_back(item);
    }
    if (filtered.size() == all.size()) return false;
    save(filtered);
    return true;
}

void AssetRepository::resetToDefaults()
{
    m_memory = initialSeedAssets();
    if (m_storage) m_storage->write(js::stringify(initialSeedAssets()));
}

std::string AssetRepository::exportJson() const
{
    return js::stringify(getAll(), 2);
}

OpResult AssetRepository::bulkAdd(const Json& assets)
{
    const Json existing = getAll();
    std::set<std::string> ids;
    for (const auto& a : existing) ids.insert(lowerId(a));

    Json merged = Json::array();
    int added = 0;
    for (const auto& a : assets) {
        if (!ids.count(lowerId(a))) {
            merged.push_back(a);
            ++added;
        }
    }
    for (const auto& a : existing) merged.push_back(a);
    save(merged);
    return {true, "", added};
}

void AssetRepository::setAssets(const Json& assets)
{
    save(assets);
}

int AssetRepository::purgeTestAssets()
{
    const Json all = getAll();
    Json filtered = Json::array();
    for (const auto& a : all) {
        if (!truthyKey(a, "isTestAsset") && !js::startsWith(idOf(a), "test-")) filtered.push_back(a);
    }
    const int purged = static_cast<int>(all.size() - filtered.size());
    if (purged > 0) save(filtered.empty() ? initialSeedAssets() : filtered);
    return purged;
}

OpResult AssetRepository::importJson(const std::string& text)
{
    Json parsed;
    try {
        parsed = js::parse(text);
    } catch (const std::exception& e) {
        return {false, std::string("Invalid JSON syntax: ") + e.what(), std::nullopt};
    }

    if (!parsed.is_array()) {
        return {false, "Imported JSON must be an array of asset records.", std::nullopt};
    }

    for (const auto& item : parsed) {
        if (!truthyKey(item, "id") || !truthyKey(item, "name") || !truthyKey(item, "domain")) {
            const Json& id = field(item, "id");
            const std::string shown = (!has(item, "id") || id.is_null()) ? "unknown" : str(id);
            return {false, "Asset record missing required attributes (id: " + shown + ")", std::nullopt};
        }
    }

    save(parsed);
    return {true, "", static_cast<int>(parsed.size())};
}

void AssetRepository::save(const Json& assets)
{
    m_memory = assets;
    if (m_storage) m_storage->write(js::stringify(assets));
}

/* ============================================================
   TELEMETRY  (registry/telemetryEngine.ts)
   ============================================================ */

Json IngestResult::toJson() const
{
    Json j;
    j["success"] = success;
    if (reading) j["reading"] = *reading;
    if (!error.empty()) j["error"] = error;
    return j;
}

Json CsvImportResult::toJson() const
{
    Json j;
    j["success"] = success;
    j["importedCount"] = importedCount;
    j["errors"] = errors;
    return j;
}

Json validateTelemetryReading(const Json& reading)
{
    Json j;
    if (!truthyKey(reading, "assetId") || !truthyKey(reading, "channelId")) {
        j["valid"] = false;
        j["error"] = "Asset ID and Channel ID are mandatory.";
        return j;
    }
    const Json& v = field(reading, "value");
    if (!v.is_number() || !std::isfinite(v.get<double>())) {
        j["valid"] = false;
        j["error"] = "Reading value must be a valid finite number.";
        return j;
    }
    if (!truthyKey(reading, "timestamp")) {
        j["valid"] = false;
        j["error"] = "Timestamp is required.";
        return j;
    }
    j["valid"] = true;
    return j;
}

namespace {

double timestampMs(const Json& timestamp)
{
    if (timestamp.is_number()) return timestamp.get<double>();
    if (timestamp.is_string()) return js::parseDate(timestamp.get<std::string>());
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

bool detectStaleTelemetry(const Json& timestamp, double nowMs, double thresholdSeconds)
{
    const double ts = timestampMs(timestamp);
    if (std::isnan(ts)) return true;
    return (nowMs - ts) / 1000 > thresholdSeconds;
}

std::string evaluateReadingQuality(double value, std::optional<double> expectedMin,
                                   std::optional<double> expectedMax,
                                   const std::optional<std::string>& timestamp, double nowMs,
                                   double staleThreshold)
{
    if (timestamp && !timestamp->empty() && detectStaleTelemetry(Json(*timestamp), nowMs, staleThreshold)) {
        return "STALE";
    }
    if (expectedMin && value < *expectedMin) return "OUT_OF_RANGE";
    if (expectedMax && value > *expectedMax) return "OUT_OF_RANGE";
    return "VALID";
}

Json parseTelemetryCsv(const std::string& csvText, double nowMs)
{
    const auto lines = splitLines(js::trim(csvText));
    Json results = Json::array();
    if (lines.size() < 2) return results;

    std::vector<std::string> headers;
    for (const auto& h : splitComma(lines[0])) headers.push_back(js::toLower(h));

    const int assetIdx = findHeader(headers, {"assetid", "asset"});
    const int channelIdx = findHeader(headers, {"channelid", "channel"});
    const int valueIdx = findHeader(headers, {"value", "reading"});
    const int unitIdx = findHeader(headers, {"unit"});
    const int timeIdx = findHeader(headers, {"timestamp", "time"});
    const int sourceIdx = findHeader(headers, {"source"});

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const auto cols = splitComma(lines[i]);
        const std::string* raw = col(cols, valueIdx);
        const double val = raw ? js::parseFloat(*raw) : std::numeric_limits<double>::quiet_NaN();
        if (std::isnan(val)) continue;

        Json r;
        auto put = [&](const char* key, int idx, const std::string& fallback, bool useFallback) {
            if (idx != -1) {
                if (const auto* c = col(cols, idx)) r[key] = *c;
            } else if (useFallback) {
                r[key] = fallback;
            }
        };
        put("assetId", assetIdx, "", true);
        put("channelId", channelIdx, "", true);
        r["value"] = val;
        put("unit", unitIdx, "", true);
        put("timestamp", timeIdx, js::toISOString(nowMs), true);
        put("source", sourceIdx, "imported_data", true);
        r["quality"] = "good";
        results.push_back(r);
    }
    return results;
}

TelemetryEngine::TelemetryEngine(Clock clock, double staleThresholdSeconds)
    : m_clock(std::move(clock)), m_staleThresholdSeconds(staleThresholdSeconds)
{
}

std::string TelemetryEngine::channelKey(const std::string& assetId, const std::string& channelId)
{
    return js::toLower(assetId) + ":" + js::toLower(channelId);
}

Json* TelemetryEngine::find(const std::string& key)
{
    for (auto& e : m_readings) {
        if (e.key == key) return &e.reading;
    }
    return nullptr;
}

IngestResult TelemetryEngine::ingest(const IngestParams& params)
{
    if (!params.value.is_number() || !std::isfinite(params.value.get<double>())) {
        return {false, std::nullopt, "Reading value must be a valid finite number."};
    }
    const double value = params.value.get<double>();
    const double now = m_clock();

    double ts = 0;
    if (!truthy(params.timestamp)) {
        ts = now;
    } else {
        ts = timestampMs(params.timestamp);
    }
    if (std::isnan(ts) || ts <= 0) ts = now;
    const std::string validTimestamp = js::toISOString(ts);

    if (ts > m_clock() + 300000) {
        return {false, std::nullopt, "Reading timestamp cannot be more than 5 minutes in the future."};
    }

    const std::string key = channelKey(params.assetId, params.channelId);
    if (Json* existing = find(key)) {
        if ((*existing)["timestamp"] == validTimestamp && (*existing)["value"].get<double>() == value) {
            return {true, *existing, ""};
        }
    }

    const double ageSeconds = (m_clock() - ts) / 1000;
    const std::string quality = ageSeconds > m_staleThresholdSeconds ? "stale" : "good";

    Json reading;
    reading["assetId"] = params.assetId;
    reading["channelId"] = params.channelId;
    reading["value"] = params.value;
    reading["unit"] = params.unit;
    reading["timestamp"] = validTimestamp;
    reading["source"] = params.source;
    reading["quality"] = quality;
    reading["raw"] = params.value;
    if (params.notes) reading["notes"] = *params.notes;

    if (Json* existing = find(key)) {
        *existing = reading;
    } else {
        m_readings.push_back({key, reading});
    }

    Json* history = nullptr;
    for (auto& [k, list] : m_history) {
        if (k == key) history = &list;
    }
    if (!history) {
        m_history.emplace_back(key, Json::array());
        history = &m_history.back().second;
    }
    history->push_back(reading);
    while (history->size() > m_maxHistoryPerChannel) history->erase(history->begin());

    return {true, reading, ""};
}

std::optional<Json> TelemetryEngine::getReading(const std::string& assetId, const std::string& channelId)
{
    Json* reading = find(channelKey(assetId, channelId));
    if (!reading) return std::nullopt;

    const double readingTime = js::parseDate(stringField(*reading, "timestamp"));
    const double ageSeconds = (m_clock() - readingTime) / 1000;
    if (ageSeconds > m_staleThresholdSeconds && (*reading)["quality"] == "good") {
        (*reading)["quality"] = "stale";
    }
    return *reading;
}

Json TelemetryEngine::getHistory(const std::string& assetId, const std::string& channelId) const
{
    const std::string key = channelKey(assetId, channelId);
    for (const auto& [k, list] : m_history) {
        if (k == key) return list;
    }
    return Json::array();
}

std::string TelemetryEngine::determineOperationalState(const Json& asset)
{
    const std::string status = stringField(asset, "commissioningStatus");
    if (status == "Planned") return "planned";
    if (status == "Commissioning") return "commissioning";
    if (status == "Decommissioned") return "decommissioned";

    const Json& channels = channelsOf(asset);
    const Json* primary = nullptr;
    for (const auto& ch : channels) {
        if (has(ch, "id") && has(asset, "primaryChannelId") && ch["id"] == asset["primaryChannelId"]) {
            primary = &ch;
            break;
        }
    }
    if (!primary && !channels.empty()) primary = &channels[0];
    if (!primary) return "simulated";

    std::optional<Json> latest = getReading(idOf(asset), stringField(*primary, "id"));
    if (!latest && truthyKey(*primary, "lastReading")) latest = (*primary)["lastReading"];

    if (!latest) {
        if (stringField(*primary, "source") == "simulation") return "simulated";
        return "stale";
    }

    const double ageSeconds = (m_clock() - timestampMs(field(*latest, "timestamp"))) / 1000;
    if (ageSeconds > m_staleThresholdSeconds) return "stale";

    const std::string source = stringField(*latest, "source");
    if (source == "physical_sensor" || source == "gateway") return "active_verified";
    if (source == "manual_entry" || source == "imported_data") return "active_manual";
    return "simulated";
}

CsvImportResult TelemetryEngine::parseCsvReadings(const std::string& csvText, const std::string& assetId)
{
    const auto lines = splitLines(js::trim(csvText));
    if (lines.size() < 2) {
        return {false, 0, {"CSV file must contain a header and at least one data row."}};
    }

    std::vector<std::string> headers;
    for (const auto& h : splitComma(lines[0])) headers.push_back(js::toLower(h));

    const int channelIdx = findHeader(headers, {"channel", "channelid", "metric"});
    const int valueIdx = findHeader(headers, {"value", "reading"});
    const int timeIdx = findHeader(headers, {"time", "timestamp", "date"});
    const int unitIdx = findHeader(headers, {"unit"});

    if (channelIdx == -1 || valueIdx == -1) {
        return {false, 0, {"CSV header must include 'channel' (or 'metric') and 'value' columns."}};
    }

    int importedCount = 0;
    std::vector<std::string> errors;

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string line = js::trim(lines[i]);
        if (line.empty()) continue;

        const auto cols = splitComma(line);
        const std::string* channelId = col(cols, channelIdx);
        const std::string* rawValue = col(cols, valueIdx);
        const double numVal = rawValue ? js::parseFloat(*rawValue) : std::numeric_limits<double>::quiet_NaN();
        const std::string* timestamp = timeIdx != -1 ? col(cols, timeIdx) : nullptr;
        const std::string* unit = unitIdx != -1 ? col(cols, unitIdx) : nullptr;

        const std::string row = std::to_string(i + 1);
        if (!channelId || channelId->empty() || std::isnan(numVal)) {
            errors.push_back("Row " + row + ": Invalid channel name or numeric value.");
            continue;
        }

        IngestParams params;
        params.assetId = assetId;
        params.channelId = *channelId;
        params.value = numVal;
        params.unit = unit ? *unit : "";
        params.timestamp = timestamp ? Json(*timestamp) : Json(nullptr);
        params.source = "imported_data";

        const IngestResult res = ingest(params);
        if (res.success) {
            ++importedCount;
        } else {
            errors.push_back("Row " + row + ": " + res.error);
        }
    }

    return {importedCount > 0, importedCount, errors};
}

void TelemetryEngine::clear()
{
    m_readings.clear();
    m_history.clear();
}

/* ============================================================
   ALERTS  (registry/alertEngine.ts)
   ============================================================ */

bool isAlertSuppressedByHysteresis(double value, double threshold, double deadbandRatio, bool priorAlarmActive)
{
    if (!priorAlarmActive) return false;
    const double clearingBound = threshold * (1 - deadbandRatio);
    return value >= clearingBound;
}

AlertEngine::AlertEngine(Clock clock) : m_clock(std::move(clock)) {}

void AlertEngine::set(const std::string& key, const Json& alert)
{
    for (auto& [k, v] : m_active) {
        if (k == key) {
            v = alert;
            return;
        }
    }
    m_active.emplace_back(key, alert);
}

void AlertEngine::erase(const std::string& key)
{
    m_active.erase(std::remove_if(m_active.begin(), m_active.end(),
                                  [&](const auto& entry) { return entry.first == key; }),
                   m_active.end());
}

const Json* AlertEngine::get(const std::string& key) const
{
    for (const auto& [k, v] : m_active) {
        if (k == key) return &v;
    }
    return nullptr;
}

Json AlertEngine::evaluateChannel(const Json& asset, const Json& channel, const std::optional<Json>& reading)
{
    const std::string assetId = idOf(asset);
    const std::string channelId = stringField(channel, "id");
    const std::string key = js::toLower(assetId) + "::" + js::toLower(channelId);
    const std::string status = stringField(asset, "commissioningStatus");

    if (status == "Planned" || status == "Decommissioned") {
        erase(key);
        return nullptr;
    }

    const std::string unit = str(field(channel, "unit"));

    auto base = [&](const char* id, const Json& observed, const Json& threshold, const char* type,
                    const std::string& source, const char* severity, const std::string& detection,
                    const Json& quality, const std::string& explanation, const std::string& action,
                    bool regulatory) {
        /* undefined fields vanish from JSON, as in the browser */
        auto copy = [](Json& out, const char* key, const Json& from, const char* fromKey) {
            if (has(from, fromKey)) out[key] = from[fromKey];
        };
        Json a;
        a["id"] = std::string(id) + key;
        copy(a, "assetId", asset, "id");
        copy(a, "assetName", asset, "name");
        copy(a, "channelId", channel, "id");
        copy(a, "channelName", channel, "name");
        a["observedValue"] = observed;
        copy(a, "unit", channel, "unit");
        a["threshold"] = threshold;
        a["thresholdType"] = type;
        a["thresholdSource"] = source;
        a["severity"] = severity;
        a["detectionTimestamp"] = detection;
        if (!quality.is_null() || quality.is_string()) a["dataQuality"] = quality;
        a["explanation"] = explanation;
        a["suggestedAction"] = action;
        a["isRegulatory"] = regulatory;
        return a;
    };

    if (!reading) {
        if (status == "Active" && stringField(channel, "source") != "simulation") {
            Json stale = base("alert-stale-", 0, 0, "min", "System Heartbeat Monitor", "stale",
                              js::toISOString(m_clock()), "stale",
                              "No telemetry packets received for " + str(field(channel, "name")) +
                                  ". Telemetry channel is disconnected or unconfigured.",
                              "Check gateway physical connectivity and field sensor power supply.", false);
            set(key, stale);
            return stale;
        }
        erase(key);
        return nullptr;
    }

    const Json& rules = field(channel, "alertThresholds");
    if (!truthy(rules)) {
        erase(key);
        return nullptr;
    }

    const double val = js::toNumber(field(*reading, "value"));
    const Json& valJson = field(*reading, "value");
    const double hysteresis = truthyKey(rules, "hysteresis") ? js::toNumber(rules["hysteresis"]) : 0;
    const Json* existing = get(key);
    const std::string detection = existing && truthyKey(*existing, "detectionTimestamp")
                                      ? (*existing)["detectionTimestamp"].get<std::string>()
                                      : js::toISOString(m_clock());
    const Json& quality = field(*reading, "quality");

    auto rule = [&](const char* name) -> std::optional<double> {
        if (!has(rules, name)) return std::nullopt;
        return js::toNumber(rules[name]);
    };
    const std::string description = truthyKey(rules, "ruleDescription") ? str(rules["ruleDescription"]) : "";
    const std::string suggested = truthyKey(rules, "suggestedAction") ? str(rules["suggestedAction"]) : "";
    const std::string thresholdSource = truthyKey(rules, "thresholdSource") ? str(rules["thresholdSource"]) : "";

    bool regulatory = false;
    if (rules.contains("thresholdSource") && rules["thresholdSource"].is_string()) {
        const std::string lower = js::toLower(rules["thresholdSource"].get<std::string>());
        regulatory = js::includes(lower, "cerc") || js::includes(lower, "cpcb");
    }

    const std::string v = js::toString(val);

    if (auto alarmMax = rule("alarmMax"); alarmMax && val > *alarmMax) {
        Json a = base("alert-alarm-max-", valJson, rules["alarmMax"], "max",
                      thresholdSource.empty() ? "Engineering Limit Specification" : thresholdSource, "alarm",
                      detection, quality,
                      description.empty() ? "Observed " + v + " " + unit + " exceeds critical upper alarm ceiling (" +
                                                str(rules["alarmMax"]) + " " + unit + ")."
                                          : description,
                      suggested.empty() ? "Inspect equipment immediately and shed demand." : suggested, regulatory);
        set(key, a);
        return a;
    }

    if (auto alarmMin = rule("alarmMin"); alarmMin && val < *alarmMin) {
        Json a = base("alert-alarm-min-", valJson, rules["alarmMin"], "min",
                      thresholdSource.empty() ? "Engineering Limit Specification" : thresholdSource, "alarm",
                      detection, quality,
                      description.empty() ? "Observed " + v + " " + unit +
                                                " dropped below critical minimum operating threshold (" +
                                                str(rules["alarmMin"]) + " " + unit + ")."
                                          : description,
                      suggested.empty() ? "Initiate auxiliary supply or check system pressure/charge." : suggested,
                      regulatory);
        set(key, a);
        return a;
    }

    if (auto normalMax = rule("normalMax"); normalMax && val > *normalMax) {
        if (existing && (*existing)["severity"] == "alarm" && val > *normalMax - hysteresis) return *existing;

        Json a = base("alert-watch-max-", valJson, rules["normalMax"], "max",
                      thresholdSource.empty() ? "Operational Envelope Guideline" : thresholdSource, "watch",
                      detection, quality,
                      description.empty() ? "Observed " + v + " " + unit + " is outside normal envelope band (" +
                                                str(rules["normalMax"]) + " " + unit + ")."
                                          : description,
                      suggested.empty() ? "Monitor trend for persistent deviation." : suggested, false);
        set(key, a);
        return a;
    }

    if (auto normalMin = rule("normalMin"); normalMin && val < *normalMin) {
        if (existing && (*existing)["severity"] == "alarm" && val < *normalMin + hysteresis) return *existing;

        Json a = base("alert-watch-min-", valJson, rules["normalMin"], "min",
                      thresholdSource.empty() ? "Operational Envelope Guideline" : thresholdSource, "watch",
                      detection, quality,
                      description.empty() ? "Observed " + v + " " + unit + " is below normal lower band (" +
                                                str(rules["normalMin"]) + " " + unit + ")."
                                          : description,
                      suggested.empty() ? "Monitor trend and verify sensor calibration." : suggested, false);
        set(key, a);
        return a;
    }

    erase(key);
    return nullptr;
}

Json AlertEngine::evaluateAll(const Json& assets, const ReadingLookup& getReading)
{
    Json results = Json::array();
    for (const auto& asset : assets) {
        for (const auto& channel : channelsOf(asset)) {
            std::optional<Json> reading = getReading(idOf(asset), stringField(channel, "id"));
            if (!reading && truthyKey(channel, "lastReading")) reading = channel["lastReading"];
            Json alert = evaluateChannel(asset, channel, reading);
            if (!alert.is_null()) results.push_back(alert);
        }
    }
    return results;
}

Json AlertEngine::getActiveAlerts() const
{
    Json out = Json::array();
    for (const auto& [k, v] : m_active) out.push_back(v);
    return out;
}

Json AlertEngine::getAlertForAsset(const std::string& assetId) const
{
    const std::string wanted = js::toLower(assetId);
    Json out = Json::array();
    for (const auto& [k, v] : m_active) {
        if (js::toLower(str(field(v, "assetId"))) == wanted) out.push_back(v);
    }
    return out;
}

Json evaluateAssetAlerts(const Json& asset, const Json& readingsByChannel, double nowMs)
{
    AlertEngine engine([nowMs] { return nowMs; });
    Json alerts = Json::array();
    for (const auto& ch : channelsOf(asset)) {
        std::optional<Json> reading;
        const std::string id = stringField(ch, "id");
        if (readingsByChannel.is_object() && readingsByChannel.contains(id) && truthy(readingsByChannel[id])) {
            reading = readingsByChannel[id];
        } else if (truthyKey(ch, "lastReading")) {
            reading = ch["lastReading"];
        }
        Json alert = engine.evaluateChannel(asset, ch, reading);
        if (!alert.is_null()) alerts.push_back(alert);
    }
    return alerts;
}

/* ============================================================
   TEST LAB  (registry/testLab.ts)
   ============================================================ */

namespace {

struct Parcel {
    const char* id;
    const char* name;
    double minX, maxX, minY, maxY;
};

const Parcel PARCELS[6] = {
    {"p1", "Campus Center · Zone 1", 300, 480, 200, 350},
    {"p2", "Solar Array West · Zone 2", 80, 260, 100, 260},
    {"p3", "Water Kiosk & STP · Zone 3", 200, 360, 320, 460},
    {"p4", "Shared Kitchen & Hub · Zone 4", 420, 560, 160, 300},
    {"p5", "Residence Blocks · Zone 5", 520, 720, 120, 380},
    {"p6", "Bioreserve Wilds · Zone 6", 80, 740, 400, 480},
};

struct Timings {
    double filterMs;
    double searchMs;
    double sortMs;
    double memoryKb;
};

Timings measure(const Json& assets)
{
    double t0 = steadyMs();
    std::size_t energy = 0;
    for (const auto& a : assets) energy += field(a, "domain") == "Energy" ? 1 : 0;
    double t1 = steadyMs();
    const double filterMs = t1 - t0;

    t0 = steadyMs();
    std::size_t matches = 0;
    for (const auto& a : assets) {
        matches += (js::includes(js::toLower(stringField(a, "name")), "05") || js::includes(idOf(a), "05")) ? 1 : 0;
    }
    t1 = steadyMs();
    const double searchMs = t1 - t0;

    t0 = steadyMs();
    std::vector<std::string> names;
    for (const auto& a : assets) names.push_back(stringField(a, "name"));
    std::sort(names.begin(), names.end());
    t1 = steadyMs();
    const double sortMs = t1 - t0;
    (void)energy;
    (void)matches;

    Json sample = Json::array();
    for (std::size_t i = 0; i < assets.size() && i < 10; ++i) sample.push_back(assets[i]);
    const double bytes = assets.empty()
                             ? 0
                             : static_cast<double>(js::utf16Length(js::stringify(sample))) *
                                   (static_cast<double>(assets.size()) / 10);
    return {filterMs, searchMs, sortMs, js::round(bytes / 1024)};
}

}  // namespace

Json generateTestAssets(const TestLabConfig& config, double nowMs)
{
    const double start = steadyMs();
    const std::string datasetTag = config.datasetTag && !config.datasetTag->empty()
                                       ? *config.datasetTag
                                       : "test-run-" + base36(nowMs);
    const int count = config.count;
    const double includeCoordPct = config.includeCoordinatesPercentage.value_or(75);
    const double includeAlertPct = config.includeAlertsPercentage.value_or(10);

    const Json& templates = assetTemplateCatalog();
    const std::size_t templateCount = templates.size();

    Json assets = Json::array();
    Json initialReadings = Json::array();

    for (int i = 1; i <= count; ++i) {
        const Json& t = templates[static_cast<std::size_t>(i - 1) % templateCount]["template"];
        const Parcel& parcel = PARCELS[static_cast<std::size_t>(i - 1) % 6];

        const bool hasCoords = (i * 17) % 100 < includeCoordPct;
        const bool isAlerting = (i * 31) % 100 < includeAlertPct;

        const std::string domain = js::toLower(stringField(t, "domain"));
        const std::string id = "test-" + domain + "-" +
                               js::padStart(std::to_string(i), count >= 1000 ? 4 : 3, '0');
        const std::string name = "Simulated " + stringField(t, "type") + " #" + std::to_string(i);

        Json mapPosition = nullptr;
        if (hasCoords) {
            mapPosition = Json::object();
            mapPosition["x"] = js::round(parcel.minX + std::fmod(i * 37.0, parcel.maxX - parcel.minX));
            mapPosition["y"] = js::round(parcel.minY + std::fmod(i * 43.0, parcel.maxY - parcel.minY));
        }

        Json channels = Json::array();
        for (const auto& chTpl : channelsOf(t)) {
            const std::string channelId = id + "-" + stringField(chTpl, "id");
            Json c = chTpl;
            c["id"] = channelId;
            c["channelId"] = channelId;
            c["assetId"] = id;
            c["source"] = "simulation";
            channels.push_back(c);
        }

        const Json* primary = channels.empty() ? nullptr : &channels[0];

        double primaryValue =
            js::toNumber(field(t, "defaultCapacity")) * (0.6 + static_cast<double>((i * 13) % 35) / 100);
        std::string quality = "good";

        if (isAlerting && primary && truthy(field(field(*primary, "alertThresholds"), "alarmMax"))) {
            primaryValue = js::toNumber((*primary)["alertThresholds"]["alarmMax"]) * 1.15;
            quality = "degraded";
        }

        Json reading;
        reading["assetId"] = id;
        reading["channelId"] = primary ? (*primary)["id"] : Json("ch-1");
        reading["value"] = js::round(primaryValue * 100) / 100;
        reading["unit"] = primary ? field(*primary, "unit") : field(t, "capacityUnit");
        reading["timestamp"] = js::toISOString(nowMs - ((i * 7) % 30) * 1000.0);
        reading["source"] = "simulation";
        reading["quality"] = quality;
        reading["notes"] = "Generated by Dhun Twin OS Repeatable Test Lab";
        initialReadings.push_back(reading);

        const char* status = i % 15 == 0 ? "Commissioning" : "Active";

        Json location;
        location["parcelId"] = parcel.id;
        location["parcelName"] = parcel.name;
        location["hasVerifiedCoordinates"] = hasCoords;
        if (hasCoords) {
            location["mapPosition"] = mapPosition;
            Json coords;
            coords["lat"] = 26.9124 + (mapPosition["y"].get<double>() / 500) * 0.015;
            coords["lng"] = 75.7873 + (mapPosition["x"].get<double>() / 800) * 0.02;
            location["coordinates"] = coords;
        }

        Json asset;
        asset["id"] = id;
        asset["assetId"] = id;
        asset["name"] = name;
        asset["type"] = field(t, "type");
        asset["assetType"] = field(t, "type");
        asset["subtype"] = field(t, "subtype");
        asset["assetSubtype"] = field(t, "subtype");
        asset["domain"] = field(t, "domain");
        asset["description"] = stringField(t, "description") + " [Synthetic Test Lab Instance]";
        asset["commissioningStatus"] = status;
        asset["lifecycleStatus"] = status;
        asset["connectivityStatus"] = "Connected";
        asset["monitoringMode"] = "Simulated";
        asset["ratedCapacity"] = field(t, "defaultCapacity");
        asset["capacityUnit"] = field(t, "capacityUnit");
        asset["location"] = location;
        asset["channels"] = channels;
        if (primary) asset["primaryChannelId"] = (*primary)["id"];
        asset["evidenceClassification"] = "Simulated";
        asset["verificationStatus"] = "verified";
        asset["isTestAsset"] = true;
        asset["testDatasetId"] = datasetTag;
        asset["createdAt"] = js::toISOString(nowMs);
        asset["updatedAt"] = js::toISOString(nowMs);
        asset["lastSeenAt"] = reading["timestamp"];
        Json carbon;
        carbon["includedInCampusMeter"] = true;
        carbon["eligibleForLedger"] = false;
        asset["carbonAccounting"] = carbon;
        assets.push_back(asset);
    }

    const double generationMs = round2(steadyMs() - start);
    const Timings t = measure(assets);

    Json benchmark;
    benchmark["assetCount"] = count;
    benchmark["generationMs"] = generationMs;
    benchmark["filterMs"] = round2(t.filterMs);
    benchmark["searchMs"] = round2(t.searchMs);
    benchmark["sortMs"] = round2(t.sortMs);
    benchmark["memoryEstimatedKb"] = t.memoryKb;

    Json out;
    out["assets"] = assets;
    out["initialReadings"] = initialReadings;
    out["benchmark"] = benchmark;
    return out;
}

Json purgeTestAssetsFromList(const Json& assets)
{
    Json out = Json::array();
    for (const auto& a : assets) {
        if (!truthyKey(a, "isTestAsset") && !js::startsWith(idOf(a), "test-")) out.push_back(a);
    }
    return out;
}

Json generateTestDataset(int count, double nowMs)
{
    TestLabConfig config;
    config.count = count;
    Json res = generateTestAssets(config, nowMs);
    Json out;
    out["assets"] = res["assets"];
    out["durationMs"] = res["benchmark"]["generationMs"];
    out["readings"] = res["initialReadings"];
    return out;
}

Json benchmarkRegistryPerformance(const Json& assets)
{
    const Timings t = measure(assets);
    Json j;
    j["datasetSize"] = assets.size();
    j["filterTimeMs"] = round2(t.filterMs);
    j["searchTimeMs"] = round2(t.searchMs);
    j["sortTimeMs"] = round2(t.sortMs);
    j["estimatedMemoryKb"] = t.memoryKb;
    return j;
}

/* ============================================================
   REGISTRY SERVICE  (registry/AssetRegistryContext.tsx)
   ============================================================ */

AssetRegistry::AssetRegistry(std::unique_ptr<RegistryStorage> storage, Clock clock)
    : m_repo(std::move(storage)), m_clock(clock), m_telemetry(clock), m_alerts(clock)
{
    m_assets = m_repo.getAll();
}

void AssetRegistry::refreshAssets()
{
    m_assets = m_repo.getAll();
}

Json AssetRegistry::alerts()
{
    return m_alerts.evaluateAll(m_assets, [this](const std::string& a, const std::string& c) {
        return m_telemetry.getReading(a, c);
    });
}

Json AssetRegistry::selectedAsset() const
{
    if (!m_selected) return nullptr;
    for (const auto& a : m_assets) {
        if (idOf(a) == *m_selected) return a;
    }
    return nullptr;
}

std::optional<Json> AssetRegistry::getAssetById(const std::string& id) const
{
    const std::string wanted = js::toLower(id);
    for (const auto& a : m_assets) {
        if (lowerId(a) == wanted) return a;
    }
    return std::nullopt;
}

OpResult AssetRegistry::addAsset(const Json& asset)
{
    OpResult res = m_repo.add(asset);
    if (res.success) refreshAssets();
    return res;
}

OpResult AssetRegistry::updateAsset(const Json& asset)
{
    OpResult res = m_repo.update(asset);
    if (res.success) refreshAssets();
    return res;
}

bool AssetRegistry::deleteAsset(const std::string& id)
{
    const bool res = m_repo.remove(id);
    if (res) {
        refreshAssets();
        if (m_selected && *m_selected == id) m_selected.reset();
    }
    return res;
}

void AssetRegistry::resetDefaults()
{
    m_repo.resetToDefaults();
    refreshAssets();
    m_selected.reset();
    m_testBenchmark = nullptr;
}

std::string AssetRegistry::exportJson() const
{
    return m_repo.exportJson();
}

OpResult AssetRegistry::importJson(const std::string& text)
{
    OpResult res = m_repo.importJson(text);
    if (res.success) refreshAssets();
    return res;
}

IngestResult AssetRegistry::recordReading(const Json& params)
{
    IngestParams p;
    p.assetId = stringField(params, "assetId");
    p.channelId = stringField(params, "channelId");
    p.value = field(params, "value");
    p.unit = truthyKey(params, "unit") ? str(params["unit"]) : "";
    p.timestamp = field(params, "timestamp");
    p.source = stringField(params, "source");
    if (has(params, "notes") && params["notes"].is_string()) p.notes = params["notes"].get<std::string>();
    return m_telemetry.ingest(p);
}

CsvImportResult AssetRegistry::importCsvReadings(const std::string& csvText, const std::string& assetId)
{
    return m_telemetry.parseCsvReadings(csvText, assetId);
}

std::optional<Json> AssetRegistry::getReading(const std::string& assetId, const std::string& channelId)
{
    return m_telemetry.getReading(assetId, channelId);
}

Json AssetRegistry::getHistory(const std::string& assetId, const std::string& channelId) const
{
    return m_telemetry.getHistory(assetId, channelId);
}

std::string AssetRegistry::getOperationalState(const Json& asset)
{
    return m_telemetry.determineOperationalState(asset);
}

Json AssetRegistry::filterAssets(const Json& options)
{
    const std::string query = js::toLower(js::trim(stringField(options, "searchQuery")));

    auto opt = [&](const char* key) -> std::string {
        const Json& v = field(options, key);
        return truthy(v) && v.is_string() ? v.get<std::string>() : std::string();
    };
    const std::string domain = opt("domain");
    const std::string status = opt("status");
    const std::string opState = opt("operationalState");
    const std::string source = opt("telemetrySource");
    const std::string severity = opt("alertSeverity");
    const bool coordsOnly = truthyKey(options, "hasCoordinatesOnly");

    Json out = Json::array();
    for (const auto& asset : m_assets) {
        if (!query.empty()) {
            auto match = [&](const Json& v) { return v.is_string() && js::includes(js::toLower(v.get<std::string>()), query); };
            const Json& location = field(asset, "location");
            const bool hit = match(field(asset, "name")) || match(field(asset, "id")) || match(field(asset, "type")) ||
                             match(field(asset, "assetSubtype")) || match(field(asset, "domain")) ||
                             match(field(location, "parcelName")) || match(field(asset, "manufacturer")) ||
                             match(field(asset, "model"));
            if (!hit) continue;
        }

        if (!domain.empty() && domain != "all" && stringField(asset, "domain") != domain) continue;

        if (!status.empty() && status != "all") {
            if (stringField(asset, "lifecycleStatus") != status && stringField(asset, "commissioningStatus") != status) {
                continue;
            }
        }

        if (!opState.empty() && opState != "all" && getOperationalState(asset) != opState) continue;

        if (!source.empty() && source != "all") {
            bool any = false;
            for (const auto& ch : channelsOf(asset)) any = any || stringField(ch, "source") == source;
            if (!any) continue;
        }

        if (!severity.empty() && severity != "all") {
            bool any = false;
            for (const auto& a : m_alerts.getAlertForAsset(idOf(asset))) any = any || a["severity"] == severity;
            if (!any) continue;
        }

        if (coordsOnly && !truthy(field(field(asset, "location"), "hasVerifiedCoordinates"))) continue;

        out.push_back(asset);
    }
    return out;
}

Json AssetRegistry::counts()
{
    const Json activeAlerts = alerts();
    std::set<std::string> alerting;
    for (const auto& a : activeAlerts) alerting.insert(js::toLower(str(a["assetId"])));

    int active = 0, live = 0, simulated = 0, stale = 0, commissioning = 0, alertingCount = 0, test = 0;
    for (const auto& a : m_assets) {
        const std::string lifecycle = stringField(a, "lifecycleStatus");
        const std::string commission = stringField(a, "commissioningStatus");
        const std::string mode = stringField(a, "monitoringMode");

        if (lifecycle == "Active" || commission == "Active") ++active;

        if (mode == "Live" || getOperationalState(a) == "active_verified") ++live;

        bool simChannel = false;
        for (const auto& ch : channelsOf(a)) simChannel = simChannel || stringField(ch, "source") == "simulation";
        if (mode == "Simulated" || getOperationalState(a) == "simulated" || simChannel) ++simulated;

        if (mode == "Stale" || mode == "Disconnected" || getOperationalState(a) == "stale") ++stale;

        if (lifecycle == "Commissioning" || commission == "Commissioning" ||
            getOperationalState(a) == "commissioning") {
            ++commissioning;
        }

        if (alerting.count(lowerId(a))) ++alertingCount;
        if (truthyKey(a, "isTestAsset")) ++test;
    }

    Json j;
    j["totalCount"] = m_assets.size();
    j["activeCount"] = active;
    j["liveCount"] = live;
    j["simulatedCount"] = simulated;
    j["staleCount"] = stale;
    j["commissioningCount"] = commissioning;
    j["alertingCount"] = alertingCount;
    j["testAssetCount"] = test;
    j["isTestModeActive"] = test > 0;
    return j;
}

void AssetRegistry::loadTestScenario(int count, const std::optional<std::string>& /*domain*/)
{
    if (count == 0) {
        m_repo.setAssets(Json::array());
        refreshAssets();
        m_selected.reset();
        Json bench;
        bench["datasetSize"] = 0;
        bench["generationTimeMs"] = 0.1;
        bench["filterTimeMs"] = 0.1;
        bench["searchTimeMs"] = 0.1;
        bench["sortTimeMs"] = 0.1;
        bench["memoryKb"] = 0;
        m_testBenchmark = bench;
        return;
    }

    const Json dataset = generateTestDataset(count, m_clock());
    const Json& testAssets = dataset["assets"];

    const Json bench = benchmarkRegistryPerformance(testAssets);
    Json b;
    b["datasetSize"] = count;
    b["generationTimeMs"] = dataset["durationMs"];
    b["filterTimeMs"] = bench["filterTimeMs"];
    b["searchTimeMs"] = bench["searchTimeMs"];
    b["sortTimeMs"] = bench["sortTimeMs"];
    b["memoryKb"] = bench["estimatedMemoryKb"];
    m_testBenchmark = b;

    /* Sample telemetry for the first batch (channels that carry a
       lastReading; generated channels carry none, as upstream). */
    for (std::size_t i = 0; i < testAssets.size() && i < 100; ++i) {
        const Json& a = testAssets[i];
        for (const auto& ch : channelsOf(a)) {
            if (!truthyKey(ch, "lastReading")) continue;
            IngestParams p;
            p.assetId = idOf(a);
            p.channelId = stringField(ch, "id");
            p.value = field(ch["lastReading"], "value");
            p.unit = str(field(ch, "unit"));
            p.source = "simulation";
            p.timestamp = field(ch["lastReading"], "timestamp");
            m_telemetry.ingest(p);
        }
    }

    m_repo.setAssets(testAssets);
    refreshAssets();
    if (!testAssets.empty()) {
        m_selected = idOf(testAssets[0]);
    } else {
        m_selected.reset();
    }
}

int AssetRegistry::purgeTestAssets()
{
    const int removed = m_repo.purgeTestAssets();
    refreshAssets();
    m_selected.reset();
    m_testBenchmark = nullptr;
    return removed;
}

Json AssetRegistry::snapshot()
{
    Json j;
    j["assets"] = m_assets;
    j["alerts"] = alerts();
    j["selectedAssetId"] = m_selected ? Json(*m_selected) : Json(nullptr);
    j["counts"] = counts();
    j["testBenchmark"] = m_testBenchmark;
    return j;
}

}  // namespace dhun
