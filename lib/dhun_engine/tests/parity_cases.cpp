#include "parity_cases.h"
#include <limits>

#include "dhun/commons.h"
#include "dhun/planning.h"
#include "dhun/registry.h"
#include "dhun/simulation.h"
#include "dhun/twin_frame.h"

namespace dhun::test {

namespace {

PhaseId phaseOf(const Json& value)
{
    return *parsePhase(value.get<std::string>());
}

void simulationCase(const Json& golden, Report& r)
{
    for (const auto& item : golden["twins"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        const double ms = item["ms"].get<double>();
        const auto twin = buildTwin(phase, SiteTime::fromEpochMs(ms));
        compare(item["twin"], toJson(twin),
                "twin[" + item["phase"].get<std::string>() + "@" + js::toString(ms) + "]", r);
    }

    for (const auto& item : golden["annual"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        compare(item["energy"], toJson(annualEnergy(phase)), "annual." + item["phase"].get<std::string>(), r);
        compare(item["landscape"], Json(landscapeWaterKlYr(phase)), "landscape", r);
    }

    for (const auto& item : golden["shapes"]) {
        const double f = item["f"].get<double>();
        compare(item["solar"], Json(solarShape(f)), "solarShape", r);
        compare(item["grid"], Json(gridIntensity(f)), "gridIntensity", r);
    }
}

void planningCase(const Json& golden, Report& r)
{
    for (const auto& item : golden["ledger"]) {
        compare(item["ledger"], buildLedger(phaseOf(item["phase"]), item["options"]),
                "ledger." + item["phase"].get<std::string>() + js::stringify(item["options"]), r);
    }

    const auto& opts = golden["scenarioOptions"];
    compare(opts["COMPUTE_LOAD_KW"], computeLoadKw(), "COMPUTE_LOAD_KW", r);
    compare(opts["COMPUTE_LABEL"], computeLabels(), "COMPUTE_LABEL", r);
    compare(opts["COOLING_PROFILE"], coolingProfiles(), "COOLING_PROFILE", r);
    compare(opts["COOLING_LABEL"], coolingLabels(), "COOLING_LABEL", r);

    for (const auto& item : golden["defaults"]) {
        compare(item["inputs"], defaultScenario(phaseOf(item["phase"])), "defaultScenario", r);
    }
    for (const auto& item : golden["scenarios"]) {
        compare(item["result"], runScenario(phaseOf(item["phase"]), item["inputs"]),
                "runScenario." + item["phase"].get<std::string>() + js::stringify(item["inputs"]), r);
    }
    for (const auto& item : golden["intensity"]) {
        compare(item["value"], Json(marginalIntensity(phaseOf(item["phase"]), item["hour"].get<double>())),
                "marginalIntensity", r);
    }
    for (const auto& item : golden["windows"]) {
        compare(item["windows"], computeWindows(phaseOf(item["phase"])), "computeWindows", r);
    }
    compare(golden["queue"], computeQueue(), "computeQueue", r);
    for (const auto& item : golden["jobs"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        compare(item["energy"], Json(jobEnergyKwh(phase, item["job"])), "jobEnergyKwh", r);
        compare(item["carbon"], jobCarbon(phase, item["job"], item["hour"].get<double>()),
                "jobCarbon@" + js::toString(item["hour"].get<double>()), r);
    }
    for (const auto& item : golden["zones"]) {
        const auto twin = buildTwin(phaseOf(item["phase"]), SiteTime::fromEpochMs(item["ms"].get<double>()));
        compare(item["pressures"], toJson(zonePressures(twin.summary, twin.assets)), "zonePressures", r);
        compare(item["issues"], zoneIssues(twin.summary, twin.assets), "zoneIssues", r);
    }
}

std::optional<double> optArg(const Json& args, std::size_t i)
{
    if (args.size() <= i || !args[i].is_number()) return std::nullopt;
    return args[i].get<double>();
}

void commonsCase(const Json& golden, Report& r)
{
    const auto& d = golden["defaults"];
    compare(d["COMPUTE_DEFAULT_SCENARIOS"], computeDefaultScenarios(), "COMPUTE_DEFAULT_SCENARIOS", r);
    compare(d["CAMPUS_ENERGY_DEFAULTS"], campusEnergyDefaults(), "CAMPUS_ENERGY_DEFAULTS", r);
    compare(d["DEFAULT_WORKLOAD_PROFILES"], defaultWorkloadProfiles(), "DEFAULT_WORKLOAD_PROFILES", r);
    compare(d["COMPUTE_EVIDENCE_REGISTER"], computeEvidenceRegister(), "COMPUTE_EVIDENCE_REGISTER", r);

    for (const auto& item : golden["computed"]) {
        const Json outputs = calculateComputeScenario(item["inputs"]);
        compare(item["outputs"], outputs, "calculateComputeScenario" + js::stringify(item["inputs"]), r);
        compare(item["solar"], calculateSolarBatteryInteraction(item["inputs"], outputs),
                "calculateSolarBatteryInteraction", r);
    }
    for (const auto& item : golden["impacts"]) {
        const auto& a = item["args"];
        compare(item["impact"],
                calculateCampusEnergyImpact(a[0].get<double>(), a[1].get<bool>(), a[2].get<bool>(),
                                            optArg(a, 3), optArg(a, 4)),
                "calculateCampusEnergyImpact" + js::stringify(a), r);
    }
    for (const auto& item : golden["aggregate"]) {
        compare(item["aggregate"], calculateAggregateBills(item["bills"]), "calculateAggregateBills", r);
    }
    for (const auto& item : golden["reconcile"]) {
        const auto& a = item["args"];
        const double spend = a.size() > 0 ? a[0].get<double>() : 250000;
        const double tariff = a.size() > 1 ? a[1].get<double>() : 10.0;
        const Json* bills = a.size() > 2 ? &a[2] : nullptr;
        compare(item["result"], reconcileBaselineWithEvidence(spend, tariff, bills),
                "reconcileBaselineWithEvidence" + js::stringify(a).substr(0, 40), r);
    }
    for (const auto& item : golden["stacked"]) {
        const auto& a = item["args"];
        compare(item["result"],
                calculateStackedCampusDemand(a[0].get<double>(), a[1].get<double>(), a[2].get<bool>(),
                                             a[3].get<double>(), a[4].get<std::string>(),
                                             a.size() > 5 ? a[5].get<double>() : 10.0,
                                             a.size() > 6 ? a[6].get<double>() : 0.71),
                "calculateStackedCampusDemand", r);
    }
    for (const auto& item : golden["masks"]) {
        compare(item["masked"], Json(maskAccountNumber(item["acc"].get<std::string>())), "maskAccountNumber", r);
    }
    const auto& v = golden["validation"];
    for (const auto& item : v["energy"]) compare(item["result"], validateEnergyBalance(item["params"]), "validateEnergyBalance", r);
    for (const auto& item : v["water"]) compare(item["result"], validateWaterBalance(item["params"]), "validateWaterBalance", r);
    for (const auto& item : v["carbon"]) compare(item["result"], validateCarbonBoundaries(item["params"]), "validateCarbonBoundaries", r);
    for (const auto& item : v["battery"]) compare(item["result"], validateBatteryDispatch(item["params"]), "validateBatteryDispatch", r);
    for (const auto& item : v["compute"]) compare(item["result"], validateComputeMonitoring(item["params"]), "validateComputeMonitoring", r);
}

void twinCase(const Json& golden, Report& r)
{
    for (const auto& item : golden["cases"]) {
        const PhaseId phase = phaseOf(item["phase"]);
        const SiteTime at = SiteTime::fromEpochMs(item["ms"].get<double>());
        PacketMap packets;
        for (auto it = item["packets"].begin(); it != item["packets"].end(); ++it) {
            TelemetryPacket p;
            p.nodeId = it.value()["nodeId"].get<std::string>();
            p.metric = it.value()["metric"].get<std::string>();
            p.value = it.value()["value"].get<double>();
            if (it.value().contains("at")) p.at = it.value()["at"].get<double>();
            packets[it.key()] = p;
        }
        const TwinFrame frame = buildFrame(phase, at, packets, item["wallNow"].get<double>());
        const std::string where = item["phase"].get<std::string>() + "@" + js::toString(item["ms"].get<double>()) +
                                  (packets.empty() ? "" : "+packets");
        compare(item["frame"], toJson(frame), "frame." + where, r);
        compare(item["events"], buildEvents(frame), "events." + where, r);
        compare(item["actions"], buildActions(phase, at), "actions." + where, r);
    }

    for (const auto& item : golden["packets"]) {
        Json parsed = Json::array();
        for (const auto& p : parsePacket(item["raw"].get<std::string>(), 1790000009999.0)) parsed.push_back(toJson(p));
        compare(item["parsed"], parsed, "parsePacket(" + item["raw"].get<std::string>() + ")", r);
    }
}

Json slimBenchmark(const Json& bench)
{
    if (bench.is_null()) return nullptr;
    Json j;
    j["datasetSize"] = bench["datasetSize"];
    j["memoryKb"] = bench["memoryKb"];
    return j;
}

void registryCase(const Json& golden, Report& r)
{
    double now = 0;
    AssetRegistry reg(stringStorage(), [&now] { return now; });

    const auto& ops = golden["ops"];
    const auto& results = golden["results"];

    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Json& op = ops[i];
        if (op.contains("at")) now = op["at"].get<double>();
        const std::string name = op["op"].get<std::string>();
        const std::string where = "op[" + std::to_string(i) + "]." + name;
        Json got;

        if (name == "snapshot") {
            got = reg.snapshot();
            got["testBenchmark"] = slimBenchmark(got["testBenchmark"]);
        } else if (name == "counts") {
            got = reg.counts();
        } else if (name == "record") {
            got = reg.recordReading(op["params"]).toJson();
        } else if (name == "csv") {
            got = reg.importCsvReadings(op["csv"].get<std::string>(), op["assetId"].get<std::string>()).toJson();
        } else if (name == "history") {
            const auto a = op["assetId"].get<std::string>();
            const auto c = op["channelId"].get<std::string>();
            got["history"] = reg.getHistory(a, c);
            auto reading = reg.getReading(a, c);
            got["reading"] = reading ? *reading : Json(nullptr);
        } else if (name == "add") {
            const Json asset = createAssetFromTemplate(op["template"], op["params"], now);
            got["asset"] = asset;
            got["res"] = reg.addAsset(asset).toJson();
        } else if (name == "addRaw") {
            got = reg.addAsset(op["asset"]).toJson();
        } else if (name == "update") {
            got = reg.updateAsset(op["asset"]).toJson();
        } else if (name == "select") {
            reg.setSelectedAsset(op["id"].get<std::string>());
            got = nullptr;
        } else if (name == "delete") {
            got = reg.deleteAsset(op["id"].get<std::string>());
        } else if (name == "filter") {
            got = Json::array();
            for (const auto& a : reg.filterAssets(op["options"])) got.push_back(a["id"]);
        } else if (name == "export") {
            got = reg.exportJson();
        } else if (name == "import") {
            got = reg.importJson(op["text"].get<std::string>()).toJson();
        } else if (name == "reset") {
            reg.resetDefaults();
            got = nullptr;
        } else if (name == "testlab") {
            const int count = op["count"].get<int>();
            reg.loadTestScenario(count, std::nullopt);
            got = count == 0 ? Json(nullptr) : slimBenchmark(reg.testBenchmark());
        } else if (name == "purge") {
            got = reg.purgeTestAssets();
        }

        compare(results[i], got, where, r);
    }

    /* standalone helpers */
    const auto& h = golden["helpers"];
    const double at = 1793268300000.0;
    compare(h["csv"], parseTelemetryCsv("assetId,channel,value,unit,source\nsolar-central,solar-gen-kw,410,kW,gateway\nx,y,nope,kW,\nshort,row", at),
            "parseTelemetryCsv", r);

    const Json readings = Json::array({
        Json::parse(R"({"assetId":"a","channelId":"b","value":1,"timestamp":"t"})"),
        Json::parse(R"({"assetId":"","channelId":"b","value":1,"timestamp":"t"})"),
        Json::parse(R"({"assetId":"a","channelId":"b","value":1e999,"timestamp":"t"})", nullptr, false),
        Json::parse(R"({"assetId":"a","channelId":"b","value":2})"),
    });
    Json validated = Json::array();
    for (std::size_t k = 0; k < readings.size(); ++k) {
        Json reading = readings[k];
        if (k == 2) {
            reading = Json::parse(R"({"assetId":"a","channelId":"b","timestamp":"t"})");
            reading["value"] = std::numeric_limits<double>::infinity();
        }
        validated.push_back(validateTelemetryReading(reading));
    }
    compare(h["validate"], validated, "validateTelemetryReading", r);

    Json stale = Json::array();
    for (const Json& t : {Json("2026-10-29T10:04:00Z"), Json("2026-10-29T10:05:00Z"), Json("x"), Json(1793268290000.0)}) {
        stale.push_back(detectStaleTelemetry(t, at));
    }
    compare(h["stale"], stale, "detectStaleTelemetry", r);

    Json quality = Json::array({
        evaluateReadingQuality(5, 0, 10, std::nullopt, at),
        evaluateReadingQuality(-1, 0, 10, std::nullopt, at),
        evaluateReadingQuality(11, 0, 10, std::nullopt, at),
        evaluateReadingQuality(5, std::nullopt, std::nullopt, std::string("2026-10-29T09:00:00Z"), at),
    });
    compare(h["quality"], quality, "evaluateReadingQuality", r);

    compare(h["hysteresis"],
            Json::array({isAlertSuppressedByHysteresis(95, 100, 0.05, true),
                         isAlertSuppressedByHysteresis(94, 100, 0.05, true),
                         isAlertSuppressedByHysteresis(99, 100, 0.05, false)}),
            "isAlertSuppressedByHysteresis", r);

    compare(h["perAsset"],
            evaluateAssetAlerts(initialSeedAssets()[3],
                                Json::parse(R"({"compute-it-load-kw":{"assetId":"compute-commons","channelId":"compute-it-load-kw","value":130,"unit":"kW","timestamp":"2026-10-29T10:04:00Z","source":"gateway","quality":"good"}})"),
                                at),
            "evaluateAssetAlerts", r);

    TestLabConfig config;
    config.count = 12;
    config.includeCoordinatesPercentage = 40;
    config.includeAlertsPercentage = 50;
    config.datasetTag = "golden";
    Json lab = generateTestAssets(config, at);
    lab.erase("benchmark");
    compare(h["lab"], lab, "generateTestAssets", r);

    TestLabConfig three;
    three.count = 3;
    Json tags = Json::array();
    const Json threeAssets = generateTestAssets(three, at);
    for (const auto& a : threeAssets["assets"]) tags.push_back(a["testDatasetId"]);
    compare(h["labDefaultTag"], tags, "testDatasetId", r);

    compare(h["purged"], purgeTestAssetsFromList(Json::parse(R"([{"id":"test-a"},{"id":"b","isTestAsset":true},{"id":"c"}])")),
            "purgeTestAssetsFromList", r);
    compare(h["allTemplates"], allAssetTemplates(), "ALL_ASSET_TEMPLATES", r);
}

}  // namespace

const std::map<std::string, ParityCase>& parityCases()
{
    static const std::map<std::string, ParityCase> cases = {
        {"simulation", simulationCase},
        {"planning", planningCase},
        {"commons", commonsCase},
        {"twin", twinCase},
        {"registry", registryCase},
    };
    return cases;
}

}  // namespace dhun::test
