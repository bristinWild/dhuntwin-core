/* ============================================================
   HOST SMOKE TEST
   Loads the module's generated C ABI (logos_module_impl.h) the way
   logos_host's cdylib glue does — dlopen, set context, install the
   event callback, dispatch JSON-encoded calls — and checks the
   answers. Run by scripts/test-host.sh outside of nix.
   ============================================================ */

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using Json = nlohmann::json;

using dispatch_fn = char* (*)(const char*, const char*);
using methods_fn = char* (*)();
using context_fn = void (*)(const char*, const char*, const char*);
using emit_cb = void (*)(const char*, const char*, void*);
using set_emit_fn = void (*)(emit_cb, void*);

static std::vector<std::string> g_events;
static int g_failures = 0;

static void onEvent(const char* name, const char* data, void*)
{
    g_events.push_back(std::string(name) + " " + (data ? data : ""));
}

static void check(bool ok, const std::string& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "usage: host_smoke <libdhuntwin_core_impl.so>\n";
        return 2;
    }
    void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        std::cerr << dlerror() << "\n";
        return 2;
    }
    auto dispatch = reinterpret_cast<dispatch_fn>(dlsym(lib, "logos_module_dispatch"));
    auto methods = reinterpret_cast<methods_fn>(dlsym(lib, "logos_module_get_methods"));
    auto setContext = reinterpret_cast<context_fn>(dlsym(lib, "logos_module_set_context"));
    auto setEmit = reinterpret_cast<set_emit_fn>(dlsym(lib, "logos_module_set_emit_callback"));
    check(dispatch && methods && setContext && setEmit, "C ABI exports present");

    const auto dataDir = std::filesystem::temp_directory_path() / "dhuntwin_core_host_smoke";
    std::filesystem::remove_all(dataDir);
    setEmit(onEvent, nullptr);
    setContext(argv[1], "smoke-instance", dataDir.string().c_str());

    auto call = [&](const std::string& method, const Json& args = Json::array()) -> Json {
        char* raw = dispatch(method.c_str(), args.dump().c_str());
        if (!raw) return Json();
        Json value = Json::parse(raw);
        std::free(raw);
        return value;
    };
    /* methods return JSON text inside a JSON string */
    auto doc = [&](const std::string& method, const Json& args = Json::array()) -> Json {
        const Json v = call(method, args);
        return v.is_string() ? Json::parse(v.get<std::string>()) : v;
    };

    const Json list = Json::parse(methods());
    check(list.is_array() && list.size() >= 57, "logos_module_get_methods lists the API (" + std::to_string(list.size()) + ")");

    check(call("name") == "dhuntwin_core", "name()");
    check(call("getPhase") == "today", "default phase is today");
    check(call("setPhase", {"buildout"}) == "buildout", "setPhase(buildout)");
    check(!g_events.empty() && g_events.back().rfind("phaseChanged", 0) == 0, "phaseChanged event emitted");
    check(doc("setPhase", {"mars"}).contains("error"), "unknown phase is an error document");

    const Json snap = doc("twinSnapshot");
    check(snap["summary"]["phase"] == "buildout" && snap["assets"].size() == 13, "twinSnapshot: 13 campus assets");

    const Json campus = doc("campusView");
    check(campus["pressures"].contains("energy") && campus["issues"].is_array(), "campusView: pressures and issues");

    const Json ledger = doc("ledger", {"buildout", "{}"});
    check(ledger["lines"].size() == 6 && ledger["phase"] == "buildout" && ledger["wholeLifeT"].get<double>() > 0,
          "ledger(buildout)");

    const Json base = doc("defaultScenario", {"phase1"});
    const Json result = doc("runScenario", {"phase1", base.dump()});
    check(result["capexCr"].get<double>() > 0 && result.contains("lcoeInrPerKwh"), "runScenario(phase1)");

    check(call("marginalIntensity", {"today", 13}).is_number(), "marginalIntensity");
    const Json commons = doc("calculateComputeScenario", {doc("computeCommonsDefaults")["COMPUTE_DEFAULT_SCENARIOS"]["phase1"].dump()});
    check(std::abs(commons["annualFacilityEnergyMwh"].get<double>() - 227.76) < 1e-9, "compute commons Phase 1 = 227.76 MWh/yr");

    const Json live = doc("liveTwin");
    check(live["frame"]["nodes"].size() == 11 && live["feed"] == "simulated", "liveTwin: 11 instrumented nodes");
    check(call("ingestGatewayMessage", {R"({"nodeId":"grid-hub","metric":"import","value":318.4})"}) == 1, "gateway packet accepted");
    const Json live2 = doc("liveTwin");
    bool liveImport = false;
    for (const auto& n : live2["frame"]["nodes"])
        for (const auto& m : n["metrics"])
            if (n["id"] == "grid-hub" && m["key"] == "import") liveImport = m["live"] == true && m["value"] == 318.4;
    check(liveImport, "packet overrides the model on the next frame");

    const Json reg = doc("registry");
    check(reg["assets"].size() == 13 && reg["counts"]["totalCount"] == 13, "registry seeded with 13 assets");
    check(std::filesystem::exists(dataDir / "asset_registry.json"), "registry persisted in the instance data folder");

    const Json asset = doc("createAssetFromTemplate", {"ELECTRICAL_METER", R"({"id":"meter-77","name":"Meter 77"})"});
    const Json added = doc("addAsset", {asset.dump()});
    check(added["success"] == true, "addAsset(template)");
    check(doc("registry")["assets"].size() == 14, "registry grows to 14");
    check(call("deleteAsset", {"meter-77"}) == true, "deleteAsset");
    check(doc("recordReading", {R"({"assetId":"grid-hub","channelId":"grid-import-kw","value":900,"source":"gateway"})"})["success"] == true,
          "recordReading");
    bool alarm = false;
    const Json afterReading = doc("registry");
    for (const auto& a : afterReading["alerts"]) alarm = alarm || (a["assetId"] == "grid-hub" && a["severity"] == "alarm");
    check(alarm, "900 kW import raises the grid-hub alarm");

    const Json measured = doc("measure", {R"([{"lat":26.7196,"lng":75.5934},{"lat":26.7206,"lng":75.5934},{"lat":26.7206,"lng":75.5944}])"});
    check(measured["distanceMeters"].get<double>() > 200, "measure path");

    check(call("purgeTestAssets") == 0, "purgeTestAssets with none present");
    const Json bench = doc("loadTestScenario", {25, ""});
    check(bench["datasetSize"] == 25 && doc("registry")["counts"]["testAssetCount"] == 25, "test lab loads 25 assets");

    int registryEvents = 0;
    for (const auto& e : g_events) registryEvents += e.rfind("registryChanged", 0) == 0;
    check(registryEvents >= 3, "registryChanged events emitted (" + std::to_string(registryEvents) + ")");

    std::cout << (g_failures == 0 ? "host smoke: all checks passed\n" : "host smoke: FAILED\n");
    dlclose(lib);
    return g_failures == 0 ? 0 : 1;
}
