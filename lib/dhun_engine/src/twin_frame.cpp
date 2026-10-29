#include "dhun/twin_frame.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "dhun/catalog.h"
#include "dhun/constants.h"
#include "dhun/format.h"
#include "dhun/planning.h"

namespace dhun {

namespace {

constexpr double PI = 3.141592653589793;
constexpr double INF = std::numeric_limits<double>::infinity();

struct Ctx {
    PhaseId phase;
    SiteTime at;
    double tick;
    double wallNow;
    TwinSummary summary;
    std::vector<DhunAsset> assets;
    const PacketMap* packets;
    const PhaseConfig* config;
};

struct MetricSpec {
    std::string key;
    std::string label;
    std::string unit;
    double value;
    int decimals;
    std::array<double, 2> normal;
    std::array<double, 2> alarm;
    double spread;
    std::optional<double> min = std::nullopt;
    std::optional<double> max = std::nullopt;
};

int phaseRank(PhaseId id)
{
    return static_cast<int>(id);
}

double hash(double value)
{
    const double x = std::sin(value * 127.1 + 311.7) * 43758.5453123;
    return x - std::floor(x);
}

double seedOf(const std::string& text)
{
    double seed = 0;
    for (unsigned char c : text) {
        seed = std::fmod(seed * 31 + c, 100000);
    }
    return seed;
}

/* Repeatable measurement texture: slow drift plus a small
   high-frequency component. */
double wobble(double seed, double time, double amplitude)
{
    const double phase = hash(seed) * PI * 2;

    return amplitude * (0.6 * std::sin(time / 37000 + phase) +
                        0.25 * std::sin(time / 13000 + phase * 1.7) +
                        0.15 * (hash(std::floor(time / 2000) + seed) * 2 - 1));
}

std::vector<double> series(double tick, double seed, double base, double spread)
{
    std::vector<double> out;
    out.reserve(HISTORY_POINTS);
    for (int index = 0; index < HISTORY_POINTS; ++index) {
        out.push_back(base + wobble(seed, tick - (HISTORY_POINTS - 1 - index) * HISTORY_STEP_MS, spread));
    }
    return out;
}

std::string statusFor(double value, const std::array<double, 2>& normal, const std::array<double, 2>& alarm)
{
    if (value < alarm[0] || value > alarm[1]) return "alarm";
    if (value < normal[0] || value > normal[1]) return "watch";
    return "normal";
}

TwinMetric metric(const Ctx& ctx, const std::string& nodeId, const MetricSpec& spec)
{
    const double seed = seedOf(nodeId + ":" + spec.key);
    const double floor = spec.min.value_or(0);
    const double ceiling = spec.max.value_or(INF);

    const double base =
        std::min(ceiling, std::max(floor, spec.value + wobble(seed, ctx.tick, spec.spread * 0.55)));

    const TelemetryPacket* packet = nullptr;
    if (auto it = ctx.packets->find(nodeId + ":" + spec.key); it != ctx.packets->end()) {
        packet = &it->second;
    }

    const bool live = packet && std::isfinite(packet->value) &&
                      ctx.wallNow - packet->at.value_or(0) < PACKET_TTL_MS;

    const double value = live ? packet->value : base;

    std::vector<double> history = series(ctx.tick, seed, base, spec.spread);
    for (auto& point : history) point = std::min(ceiling, std::max(floor, point));

    if (live) history.back() = value;

    TwinMetric m;
    m.key = spec.key;
    m.label = spec.label;
    m.unit = spec.unit;
    m.decimals = spec.decimals;
    m.value = value;
    m.status = statusFor(value, spec.normal, spec.alarm);
    m.normal = spec.normal;
    m.alarm = spec.alarm;
    m.history = std::move(history);
    m.live = live;
    return m;
}

void availability(const Ctx& ctx, PhaseId from, TwinNode& node)
{
    node.available = phaseRank(ctx.phase) >= phaseRank(from);
    if (!node.available) {
        node.availabilityNote = std::string("Planned for ") + phaseConfig(from).shortLabel;
    }
}

TwinNode finalize(TwinNode node)
{
    if (!node.available) {
        node.status = "offline";
        return node;
    }
    const bool alarm = std::any_of(node.metrics.begin(), node.metrics.end(),
                                   [](const TwinMetric& m) { return m.status == "alarm"; });
    const bool watch = std::any_of(node.metrics.begin(), node.metrics.end(),
                                   [](const TwinMetric& m) { return m.status == "watch"; });
    node.status = alarm ? "alarm" : watch ? "watch" : "online";
    return node;
}

const DhunAsset* assetOf(const Ctx& ctx, const std::string& id)
{
    for (const auto& asset : ctx.assets) {
        if (asset.id == id) return &asset;
    }
    return nullptr;
}

double siteShape(double fraction)
{
    return 0.58 + 0.78 * std::exp(-std::pow((fraction - 0.6) / 0.15, 2)) +
           0.22 * std::exp(-std::pow((fraction - 0.82) / 0.09, 2));
}

double occupancyShape(double fraction)
{
    return clamp(std::exp(-std::pow((fraction - 0.52) / 0.12, 2)) +
                     0.35 * std::exp(-std::pow((fraction - 0.72) / 0.1, 2)),
                 0, 1);
}

double computeShape(double fraction)
{
    return 0.88 + 0.1 * std::sin(fraction * PI * 2 - 1);
}

double designPeak(const Ctx& ctx)
{
    const PhaseConfig& config = *ctx.config;
    const double building = ((config.builtM2 * config.epi) / 8760) * 1.55;
    const double compute = config.computeKw * config.computePue * 1.08;
    const double water = (config.stpKlDay * 0.5) / 24 +
                         ((config.population * LITRES_PER_PERSON_DAY) / 1000) * (0.4 / 24);
    return std::max(5.0, building + compute + water + 2);
}

TwinNode base(const char* id, const char* name, const char* shortName, const char* kind, double x,
              double y, int plannedChannels, const char* deviceId, const char* protocol,
              const char* gateway, const char* description, const char* primaryKey)
{
    TwinNode n;
    n.id = id;
    n.name = name;
    n.shortName = shortName;
    n.kind = kind;
    n.x = x;
    n.y = y;
    n.plannedChannels = plannedChannels;
    n.deviceId = deviceId;
    n.protocol = protocol;
    n.gateway = gateway;
    n.description = description;
    n.primaryKey = primaryKey;
    return n;
}

TwinControl control(const char* key, const char* label, std::vector<std::string> options, const char* def)
{
    return TwinControl{key, label, std::move(options), def,
                       "Local demo setpoint. No command is sent to hardware."};
}

/* ---------------- node builders ---------------- */

TwinNode gridNode(const Ctx& ctx)
{
    const double peak = designPeak(ctx);
    TwinNode n = base("grid-hub", "Energy hub", "Grid hub", "grid", 655, 62, 4, "DH-GRD-01", "Modbus TCP",
                      "energy-gw-01",
                      "The 11 kV incomer and site transformer. This is the reference for every kilowatt-hour the campus imports.",
                      "import");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "grid-hub", {"import", "Grid import", "kW", ctx.summary.gridKw, 0, {0, peak * 0.92}, {0, peak * 1.05}, 7}),
        metric(ctx, "grid-hub", {"voltage", "Incoming voltage", "kV", 11, 2, {10.7, 11.3}, {10.4, 11.6}, 0.07, std::nullopt, 12}),
        metric(ctx, "grid-hub", {"frequency", "Frequency", "Hz", 49.98, 2, {49.9, 50.1}, {49.7, 50.3}, 0.035, 49, 51}),
        metric(ctx, "grid-hub", {"pf", "Power factor", "", 0.97, 2, {0.94, 1}, {0.9, 1}, 0.008, std::nullopt, 1}),
    };
    return finalize(std::move(n));
}

TwinNode solarNode(const Ctx& ctx)
{
    const PhaseConfig& config = *ctx.config;
    TwinNode n = base("solar-array", "Solar farm", "Solar", "solar", 545, 100, 4, "DH-SOL-01", "Modbus TCP",
                      "energy-gw-01",
                      "Rooftop and carport array. Irradiance and module temperature are the two signals that explain yield.",
                      "output");
    availability(ctx, PhaseId::Phase1, n);
    if (!n.available) return finalize(std::move(n));

    const double f = dayFraction(ctx.at);
    const double seasonal = MONTH_FACTOR[static_cast<std::size_t>(ctx.at.month)];
    const double shape = solarShape(f);
    const DhunAsset* solar = assetOf(ctx, "solar");
    const double output = solar ? solar->powerKw : 0;
    const double kwp = config.solarKwp;

    n.metrics = {
        metric(ctx, "solar-array", {"output", "Array output", "kW", output, 0, {0, kwp * 0.86}, {0, kwp * 0.98}, std::max(1.0, kwp * 0.004)}),
        metric(ctx, "solar-array", {"irradiance", "Plane-of-array irradiance", "W/m²", 980 * shape * seasonal, 0, {0, 1100}, {0, 1250}, 12}),
        metric(ctx, "solar-array", {"module-temp", "Module temperature", "°C", 27 + 26 * shape * seasonal, 1, {10, 72}, {0, 85}, 0.6}),
        metric(ctx, "solar-array", {"efficiency", "Inverter efficiency", "%", 97.2, 1, {94, 100}, {90, 100}, 0.4, std::nullopt, 100}),
    };
    return finalize(std::move(n));
}

TwinNode fieldOfficeNode(const Ctx& ctx)
{
    const double f = dayFraction(ctx.at);
    const double shape = siteShape(f);
    const double demand = 58 * shape * 0.82;
    const double occupied = 18 * (0.15 + 0.85 * occupancyShape(f));

    TwinNode n = base("field-office", "Field office & workshop", "Field office", "building", 540, 190, 4,
                      "DH-FO-01", "BACnet/IP", "built-gw-01",
                      "The existing field station and workshop. The only occupied building in the 2026 phase, and the first live indoor-environment record.",
                      "demand");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "field-office", {"demand", "Building demand", "kW", demand, 1, {0, 78}, {0, 95}, 0.7}),
        metric(ctx, "field-office", {"indoor-temp", "Indoor temperature", "°C", 26.2, 1, {18, 29}, {15, 32}, 0.25}),
        metric(ctx, "field-office", {"occupancy", "Occupancy", "people", occupied, 0, {0, 30}, {0, 40}, 0.7}),
        metric(ctx, "field-office", {"water", "Domestic water flow", "L/min", 4 + 9 * occupancyShape(f), 1, {0, 22}, {0, 30}, 0.35}),
    };
    n.controls = {control("cooling", "Cooling setpoint", {"24 °C", "26 °C"}, "26 °C")};
    return finalize(std::move(n));
}

TwinNode r1Node(const Ctx& ctx)
{
    const double f = dayFraction(ctx.at);
    const DhunAsset* asset = assetOf(ctx, "residences");
    const double power = asset ? asset->powerKw : 0;
    const PhaseConfig& config = *ctx.config;

    TwinNode n = base("r1-residences", "R1 residences", "R1", "building", 360, 408, 4, "DH-R1-01",
                      "BACnet/IP", "built-gw-01",
                      "The first residential block. Indoor comfort, hot-water draw and occupancy are the signals that turn a floor plan into an operating building.",
                      "demand");
    availability(ctx, PhaseId::Phase1, n);
    if (n.available) {
        n.metrics = {
            metric(ctx, "r1-residences", {"demand", "Block demand", "kW", power, 0, {0, std::max(40.0, power * 1.8)}, {0, std::max(60.0, power * 2.2)}, std::max(2.0, power * 0.015)}),
            metric(ctx, "r1-residences", {"indoor-temp", "Indoor temperature", "°C", 26.4, 1, {19, 30}, {16, 33}, 0.2}),
            metric(ctx, "r1-residences", {"dhw", "Hot-water draw", "L/min",
                                          (config.population * LITRES_PER_PERSON_DAY * 0.35) / 1440, 0,
                                          {0, (config.population * LITRES_PER_PERSON_DAY * 0.35) / 1000},
                                          {0, (config.population * LITRES_PER_PERSON_DAY * 0.55) / 1000}, 4}),
            metric(ctx, "r1-residences", {"occupancy", "Residents present", "",
                                          config.population * (0.55 + 0.45 * occupancyShape(f)), 0,
                                          {0, config.population * 1.05}, {0, config.population * 1.15}, 3}),
        };
        n.controls = {control("dhw", "Hot-water setpoint", {"45 °C", "50 °C"}, "45 °C")};
    }
    return finalize(std::move(n));
}

TwinNode schoolNode(const Ctx& ctx)
{
    const double f = dayFraction(ctx.at);
    const DhunAsset* asset = assetOf(ctx, "school");
    const double power = asset ? asset->powerKw : 0;
    const double occupancy = 300 * occupancyShape(f);

    TwinNode n = base("school", "School campus", "School", "building", 486, 418, 4, "DH-SCH-01", "BACnet/IP",
                      "built-gw-01",
                      "The seven-acre school. CO₂ and occupancy tell the ventilation story; both are invisible in an annual energy bill.",
                      "co2");
    availability(ctx, PhaseId::Phase1, n);
    if (n.available) {
        n.metrics = {
            metric(ctx, "school", {"demand", "School demand", "kW", power, 0, {0, std::max(20.0, power * 2)}, {0, std::max(30.0, power * 2.5)}, std::max(1.0, power * 0.02)}),
            metric(ctx, "school", {"co2", "Indoor CO₂", "ppm", 470 + 520 * occupancyShape(f), 0, {400, 1000}, {350, 1500}, 14}),
            metric(ctx, "school", {"indoor-temp", "Indoor temperature", "°C", 26.8, 1, {19, 31}, {16, 34}, 0.2}),
            metric(ctx, "school", {"occupancy", "Students present", "", occupancy, 0, {0, 320}, {0, 350}, 2.5}),
        };
        n.controls = {control("fresh-air", "Fresh-air mode", {"CO₂-led", "Scheduled"}, "CO₂-led")};
    }
    return finalize(std::move(n));
}

TwinNode stpNode(const Ctx& ctx)
{
    const PhaseConfig& config = *ctx.config;
    const double stpKw = (config.stpKlDay * 0.5) / 24;

    TwinNode n = base("stp", "STP & water reuse", "STP", "stp", 404, 354, 4, "DH-STP-01", "Modbus TCP",
                      "water-gw-01",
                      "Tertiary treatment. Inflow, aeration power and turbidity are the three signals that decide whether reuse is safe.",
                      "inflow");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "stp", {"inflow", "Inflow", "kL/day", config.stpKlDay, 0,
                            {config.stpKlDay * 0.75, config.stpKlDay * 1.15},
                            {config.stpKlDay * 0.55, config.stpKlDay * 1.3},
                            std::max(0.15, config.stpKlDay * 0.006)}),
        metric(ctx, "stp", {"aeration", "Aeration power", "kW", stpKw * 1.08, 2,
                            {0, std::max(0.5, stpKw * 1.5)}, {0, std::max(0.7, stpKw * 2)},
                            std::max(0.01, stpKw * 0.02)}),
        metric(ctx, "stp", {"ph", "Outlet pH", "", 7.1, 1, {6.5, 8}, {6, 8.5}, 0.05, std::nullopt, 14}),
        metric(ctx, "stp", {"turbidity", "Turbidity", "NTU", 4.2, 1, {0, 10}, {0, 20}, 0.55}),
    };
    n.controls = {control("aeration", "Aeration schedule", {"Day", "Night"}, "Day")};
    return finalize(std::move(n));
}

TwinNode borewellNode(const Ctx& ctx)
{
    const PhaseConfig& config = *ctx.config;
    const double demand = ctx.summary.waterKlDay;
    const double pumpKw = (demand * 0.4) / 24;
    const double ratio = landscapeWaterKlYr(ctx.phase) / 365 / (config.harvestKlYr / 365);

    TwinNode n = base("borewell", "Borewell & pumping", "Borewell", "water", 322, 344, 4, "DH-PMP-01", "LoRaWAN",
                      "water-gw-01",
                      "Abstraction and distribution. Discharge pressure and static level expose the hidden energy and water risk behind every tap.",
                      "abstraction");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "borewell", {"abstraction", "Abstraction", "kL/day", demand, 0,
                                 {0, std::max(1.0, demand * 1.12)}, {0, std::max(2.0, demand * 1.3)},
                                 std::max(0.3, demand * 0.008)}),
        metric(ctx, "borewell", {"pump-power", "Pump power", "kW", pumpKw, 2,
                                 {0, std::max(0.4, pumpKw * 1.4)}, {0, std::max(0.6, pumpKw * 1.8)},
                                 std::max(0.02, pumpKw * 0.04)}),
        metric(ctx, "borewell", {"pressure", "Discharge pressure", "bar", 3.4, 1, {2.2, 4.5}, {1.8, 5.2}, 0.1}),
        metric(ctx, "borewell", {"static-level", "Static water level", "m", 32 - 12 * std::min(ratio, 1.6), 1,
                                 {12, 30}, {6, 35}, 0.2, std::nullopt, 40}),
    };
    n.controls = {control("pump-window", "Pump window", {"Any", "Solar"}, "Any")};
    return finalize(std::move(n));
}

TwinNode irrigationNode(const Ctx& ctx)
{
    const PhaseConfig& config = *ctx.config;
    const double domestic = ctx.summary.waterKlDay;
    const double demand = landscapeWaterKlYr(ctx.phase) / 365;
    const double available = config.harvestKlYr / 365;
    const double ratio = (demand + domestic) / std::max(1.0, available);

    TwinNode n = base("irrigation", "Landscape & irrigation", "Irrigation", "water", 378, 300, 4, "DH-IRR-01",
                      "LoRaWAN", "water-gw-01",
                      "Domestic draw and landscape demand against harvested storage. This is the first place a dry year shows up, and the last place an annual report explains.",
                      "reservoir");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "irrigation", {"flow", "Irrigation draw", "kL/day", demand, 0, {0, available * 1.1},
                                   {0, available * 1.35}, std::max(0.5, demand * 0.008)}),
        metric(ctx, "irrigation", {"soil-moisture", "Soil moisture", "%", 30 - (ratio - 0.5) * 24, 0, {15, 45},
                                   {8, 60}, 0.5, std::nullopt, 100}),
        metric(ctx, "irrigation", {"reservoir", "Harvest reservoir", "%", 95 - ratio * 45, 0, {40, 95},
                                   {20, 100}, 0.4, std::nullopt, 100}),
        metric(ctx, "irrigation", {"pressure", "Line pressure", "bar", 2.8, 1, {1.5, 4}, {1, 4.5}, 0.08}),
    };
    n.controls = {control("irrigation-mode", "Irrigation mode", {"Moisture-led", "Scheduled"}, "Moisture-led")};
    return finalize(std::move(n));
}

TwinNode gaushalaNode(const Ctx& ctx)
{
    const double organic = ctx.config->organicTDay;

    TwinNode n = base("gaushala", "Gaushala & compost", "Gaushala", "organic", 145, 388, 3, "DH-ORG-01",
                      "LoRaWAN", "waste-gw-01",
                      "Cattle shed and aerobic compost. Feedstock and pile temperature drive biogas yield and the methane the site avoids.",
                      "biogas");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "gaushala", {"organics", "Organics intake", "t/day", organic, 2,
                                 {0, std::max(0.5, organic * 1.3)}, {0, std::max(0.8, organic * 1.7)},
                                 std::max(0.005, organic * 0.01)}),
        metric(ctx, "gaushala", {"biogas", "Biogas yield", "m³/day", organic * 28, 1,
                                 {0, std::max(8.0, organic * 38)}, {0, std::max(12.0, organic * 50)},
                                 std::max(0.2, organic * 0.5)}),
        metric(ctx, "gaushala", {"pile-temp", "Compost pile", "°C", 54, 1, {40, 68}, {30, 75}, 0.6}),
    };
    n.controls = {control("turner", "Compost turner", {"Auto", "Manual"}, "Auto")};
    return finalize(std::move(n));
}

TwinNode computeNode(const Ctx& ctx)
{
    const PhaseConfig& config = *ctx.config;
    const double f = dayFraction(ctx.at);
    const double itLoad = config.computeKw * computeShape(f);

    TwinNode n = base("compute", "Compute commons", "Compute", "compute", 672, 312, 4, "DH-CMP-01",
                      "Redfish / SNMP", "compute-gw-01",
                      "The deferrable load. IT power, PUE and queue depth are what the scheduler reads before it decides to run or wait.",
                      "it-load");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "compute", {"it-load", "IT load", "kW", itLoad, 1,
                                {0, std::max(2.0, config.computeKw * 1.25)},
                                {0, std::max(3.0, config.computeKw * 1.5)},
                                std::max(0.05, config.computeKw * 0.02)}),
        metric(ctx, "compute", {"pue", "PUE", "", config.computePue, 2, {1.1, 1.55}, {1.05, 1.75}, 0.006, 1.0}),
        metric(ctx, "compute", {"gpu-temp", "GPU temperature", "°C",
                                44 + 22 * (itLoad / std::max(0.1, config.computeKw)), 1, {35, 78}, {25, 88}, 0.4}),
        metric(ctx, "compute", {"queue", "Queue depth", "jobs", 4 + 2 * std::sin(f * PI * 2 + 0.8), 0, {0, 12},
                                {0, 20}, 0.3}),
    };
    n.controls = {control("scheduler", "Batch scheduler", {"Run now", "Solar window"}, "Run now")};
    return finalize(std::move(n));
}

TwinNode weatherNode(const Ctx& ctx)
{
    const double f = dayFraction(ctx.at);
    const double shape = solarShape(f);

    TwinNode n = base("weather", "Bioreserve weather mast", "Weather", "weather", 150, 125, 4, "DH-WX-01",
                      "LoRaWAN", "land-gw-01",
                      "The ground truth for every solar and irrigation assumption. Ambient temperature, humidity, wind and rain, sampled inside the protected bioreserve.",
                      "ambient-temp");
    availability(ctx, PhaseId::Today, n);
    n.metrics = {
        metric(ctx, "weather", {"ambient-temp", "Ambient temperature", "°C", 29 + 8 * shape, 1, {5, 46}, {0, 49}, 0.2}),
        metric(ctx, "weather", {"humidity", "Relative humidity", "%", 44 - 18 * shape, 0, {10, 90}, {5, 100}, 0.8, std::nullopt, 100}),
        metric(ctx, "weather", {"wind", "Wind speed", "m/s", 2.6, 1, {0, 14}, {0, 22}, 0.35}),
        metric(ctx, "weather", {"rain", "Rainfall", "mm/h", 0, 1, {0, 10}, {0, 30}, 0.04}),
    };
    return finalize(std::move(n));
}

std::string reading(double value, int decimals, const std::string& unit)
{
    return num(value, decimals) + (unit.empty() ? "" : " " + unit);
}

Json pair(const std::array<double, 2>& p)
{
    return Json::array({p[0], p[1]});
}

}  // namespace

/* ============================================================
   PUBLIC FRAME
   ============================================================ */

TwinFrame buildFrame(PhaseId phase, const SiteTime& at, const PacketMap& packets, double wallNowMs)
{
    const TwinSnapshot snapshot = buildTwin(phase, at);

    Ctx ctx{phase, at, at.getTime(), wallNowMs, snapshot.summary, snapshot.assets, &packets, &phaseConfig(phase)};

    TwinFrame frame;
    frame.phase = phase;
    frame.at = at;
    frame.summary = snapshot.summary;
    frame.nodes = {
        weatherNode(ctx), gridNode(ctx),   solarNode(ctx),      fieldOfficeNode(ctx),
        r1Node(ctx),      schoolNode(ctx), stpNode(ctx),        borewellNode(ctx),
        irrigationNode(ctx), gaushalaNode(ctx), computeNode(ctx),
    };

    for (const auto& node : frame.nodes) {
        if (node.status != "offline") ++frame.online;
        if (node.available) frame.sensors += static_cast<int>(node.metrics.size());
        if (node.status == "alarm") ++frame.alarms;
        if (node.status == "watch") ++frame.watches;
    }
    frame.planned = static_cast<int>(frame.nodes.size()) - frame.online;
    return frame;
}

Json buildEvents(const TwinFrame& frame)
{
    struct Event {
        Json json;
        bool alarm;
    };
    std::vector<Event> events;

    for (const auto& node : frame.nodes) {
        if (!node.available) continue;

        for (const auto& item : node.metrics) {
            if (item.status == "normal") continue;

            const double upper = item.normal[1];
            const double lower = item.normal[0];
            const bool above = item.value > upper;
            const double bound = above ? upper : lower;

            Json e;
            e["id"] = node.id + ":" + item.key;
            e["level"] = item.status == "alarm" ? "alarm" : "watch";
            e["nodeId"] = node.id;
            e["nodeName"] = node.shortName;
            e["message"] = item.label + " " + reading(item.value, item.decimals, item.unit) + " — " +
                           (above ? "above" : "below") + " the " + reading(bound, item.decimals, item.unit) +
                           " normal " + (above ? "ceiling" : "floor") + ".";
            e["at"] = siteIso(frame.at);
            events.push_back({e, item.status == "alarm"});
        }
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const Event& a, const Event& b) { return a.alarm && !b.alarm; });

    Json out = Json::array();
    for (std::size_t i = 0; i < events.size() && i < 8; ++i) out.push_back(events[i].json);
    return out;
}

Json buildActions(PhaseId phase, const SiteTime& at)
{
    const double hour = at.hours + at.minutes / 60.0;
    const double nowHour = std::floor(hour);

    const Json jobs = computeQueue();

    double runNow = 0;
    double best = 0;
    double workKwh = 0;
    std::string bestLabel = "13:00";
    bool first = true;

    for (const auto& job : jobs) {
        const Json carbon = jobCarbon(phase, job, nowHour);
        runNow += carbon["runNow"].get<double>();
        best += carbon["bestWindow"].get<double>();
        if (first) {
            bestLabel = carbon["bestLabel"].get<std::string>();
            first = false;
        }
    }

    const double computeSaving = std::max(0.0, runNow - best);
    const PhaseConfig& config = phaseConfig(phase);

    const double pumpKwh = ((config.population * LITRES_PER_PERSON_DAY) / 1000) * 0.4;

    const Json windows = computeWindows(phase);
    const Json* cleanWindow = &windows[0];
    for (const auto& item : windows) {
        if (item["intensity"].get<double>() < (*cleanWindow)["intensity"].get<double>()) cleanWindow = &item;
    }
    const double cleanIntensity = (*cleanWindow)["intensity"].get<double>();
    const std::string cleanLabel = (*cleanWindow)["label"].get<std::string>();

    const double nowIntensity = marginalIntensity(phase, nowHour);
    const double pumpSaving = (pumpKwh * (nowIntensity - cleanIntensity)) / 1000;

    const double landscapeDemand = landscapeWaterKlYr(phase) / 365;
    const double domesticDemand = (config.population * LITRES_PER_PERSON_DAY) / 1000;
    const double campusDemand = landscapeDemand + domesticDemand;
    const double harvest = config.harvestKlYr / 365;
    const double waterGap = std::max(0.0, campusDemand - harvest);

    if (computeSaving > 0.5) {
        for (const auto& job : jobs) {
            const Json carbon = jobCarbon(phase, job, nowHour);
            workKwh += carbon["runNow"].get<double>() / std::max(1e-6, nowIntensity / 1000);
        }
    }

    auto action = [](const char* id, const char* icon, const std::string& title, const std::string& detail,
                     const std::string& metric, const char* tone, const char* href, const char* cta) {
        Json j;
        j["id"] = id;
        j["icon"] = icon;
        j["title"] = title;
        j["detail"] = detail;
        j["metric"] = metric;
        j["tone"] = tone;
        j["href"] = href;
        j["cta"] = cta;
        return j;
    };

    Json out = Json::array();

    out.push_back(action(
        "compute", "compute",
        computeSaving > 0.5 ? "Move the batch queue to " + bestLabel : "Batch queue is already in a clean window",
        computeSaving > 0.5
            ? std::to_string(jobs.size()) + " jobs, " + num(workKwh, 0) +
                  " kWh of work. The same output runs against a lower marginal intensity without new hardware."
            : "The current hour is within the lowest-intensity band available today. Deferral would not reduce emissions.",
        computeSaving > 0.5 ? "−" + num(computeSaving, 1) + " kgCO₂e" : "No gain now",
        computeSaving > 0.5 ? "good" : "warn", "/compute", "Open compute"));

    out.push_back(action(
        "pump", "pump",
        pumpSaving > 0.05 ? "Pump inside the " + cleanLabel + " window" : "Pumping is already in the cleanest window",
        pumpSaving > 0.05
            ? "The borewell runs continuously. Shifting its four-hour block to " + cleanLabel +
                  " keeps the same litres moving against lower grid carbon."
            : "The pumping block already overlaps the lowest-intensity hour. There is no free carbon left in this load.",
        pumpSaving > 0.05 ? "−" + num(pumpSaving, 1) + " kgCO₂e/day" : "No gain now",
        pumpSaving > 0.05 ? "good" : "warn", "/scenarios", "Test in cockpit"));

    out.push_back(action(
        "water", "water",
        waterGap > 0.5 ? "Trim campus demand to the harvest line" : "Harvest covers the campus water schedule",
        waterGap > 0.5
            ? "Campus demand is " + num(campusDemand) + " kL/day, domestic plus landscape, against " + num(harvest) +
                  " kL/day of harvest. Closing the gap means " + num(waterGap) + " kL/day less draw, or more storage."
            : "Campus demand of " + num(campusDemand) + " kL/day sits inside the " + num(harvest) +
                  " kL/day harvest. The current schedule is within the water budget.",
        waterGap > 0.5 ? "−" + num(waterGap) + " kL/day" : "Balanced", waterGap > 0.5 ? "warn" : "good",
        "/scenarios", "Test in cockpit"));

    return out;
}

const TwinMetric* primaryMetric(const TwinNode& node)
{
    for (const auto& item : node.metrics) {
        if (item.key == node.primaryKey) return &item;
    }
    return node.metrics.empty() ? nullptr : &node.metrics.front();
}

const Json& twinStatusTable()
{
    return catalog::get("twin_status");
}

/* ============================================================
   JSON
   ============================================================ */

Json toJson(const TwinMetric& m)
{
    Json j;
    j["key"] = m.key;
    j["label"] = m.label;
    j["unit"] = m.unit;
    j["decimals"] = m.decimals;
    j["value"] = m.value;
    j["status"] = m.status;
    j["normal"] = pair(m.normal);
    j["alarm"] = pair(m.alarm);
    j["history"] = m.history;
    j["live"] = m.live;
    return j;
}

Json toJson(const TwinControl& c)
{
    Json j;
    j["key"] = c.key;
    j["label"] = c.label;
    j["options"] = c.options;
    j["default"] = c.defaultValue;
    j["note"] = c.note;
    return j;
}

Json toJson(const TwinNode& n)
{
    Json j;
    j["id"] = n.id;
    j["name"] = n.name;
    j["short"] = n.shortName;
    j["kind"] = n.kind;
    j["x"] = n.x;
    j["y"] = n.y;
    j["available"] = n.available;
    if (n.availabilityNote) j["availabilityNote"] = *n.availabilityNote;
    j["plannedChannels"] = n.plannedChannels;
    j["deviceId"] = n.deviceId;
    j["protocol"] = n.protocol;
    j["gateway"] = n.gateway;
    j["description"] = n.description;
    j["primaryKey"] = n.primaryKey;
    Json metrics = Json::array();
    for (const auto& m : n.metrics) metrics.push_back(toJson(m));
    j["metrics"] = metrics;
    Json controls = Json::array();
    for (const auto& c : n.controls) controls.push_back(toJson(c));
    j["controls"] = controls;
    j["status"] = n.status;
    return j;
}

Json toJson(const TwinFrame& f)
{
    Json j;
    j["phase"] = std::string(phaseKey(f.phase));
    j["at"] = siteIso(f.at);
    j["summary"] = toJson(f.summary);
    Json nodes = Json::array();
    for (const auto& n : f.nodes) nodes.push_back(toJson(n));
    j["nodes"] = nodes;
    j["online"] = f.online;
    j["planned"] = f.planned;
    j["sensors"] = f.sensors;
    j["alarms"] = f.alarms;
    j["watches"] = f.watches;
    return j;
}

Json toJson(const TelemetryPacket& p)
{
    Json j;
    j["nodeId"] = p.nodeId;
    j["metric"] = p.metric;
    j["value"] = p.value;
    if (p.at) j["at"] = *p.at;
    return j;
}

/* ============================================================
   PACKET PARSING  (twin/source.ts)
   ============================================================ */

namespace {

std::optional<TelemetryPacket> toPacket(const Json& value, double wallNowMs)
{
    if (!value.is_object()) return std::nullopt;

    /* Gateway envelope: { type: "telemetry", packets: [...] } */
    if (value.contains("packets") && value["packets"].is_array()) return std::nullopt;

    const auto nodeId = value.find("nodeId");
    const auto metricKey = value.find("metric");
    const auto reading = value.find("value");

    if (nodeId == value.end() || !nodeId->is_string() || nodeId->get<std::string>().empty() ||
        metricKey == value.end() || !metricKey->is_string() || metricKey->get<std::string>().empty() ||
        reading == value.end() || !reading->is_number() || !std::isfinite(reading->get<double>())) {
        return std::nullopt;
    }

    TelemetryPacket packet;
    packet.nodeId = nodeId->get<std::string>();
    packet.metric = metricKey->get<std::string>();
    packet.value = reading->get<double>();

    const auto at = value.find("at");
    packet.at = (at != value.end() && at->is_number() && std::isfinite(at->get<double>()))
                    ? at->get<double>()
                    : wallNowMs;
    return packet;
}

}  // namespace

std::vector<TelemetryPacket> parsePacket(const std::string& raw, double wallNowMs)
{
    Json parsed;
    try {
        parsed = js::parse(raw);
    } catch (...) {
        return {};
    }

    std::vector<TelemetryPacket> out;

    if (parsed.is_array()) {
        for (const auto& item : parsed) {
            if (auto p = toPacket(item, wallNowMs)) out.push_back(*p);
        }
        return out;
    }

    if (!parsed.is_object()) return out;

    if (parsed.contains("packets") && parsed["packets"].is_array()) {
        for (const auto& item : parsed["packets"]) {
            if (auto p = toPacket(item, wallNowMs)) out.push_back(*p);
        }
        return out;
    }

    if (auto p = toPacket(parsed, wallNowMs)) out.push_back(*p);
    return out;
}

/* ============================================================
   LIVE TWIN  (twin/useLiveTwin.ts)
   ============================================================ */

LiveTwin::LiveTwin(double wallNowMs) : m_now(SiteTime::fromEpochMs(wallNowMs)) {}

void LiveTwin::receive(const TelemetryPacket& packet)
{
    m_buffer[packet.nodeId + ":" + packet.metric] = packet;
}

void LiveTwin::tick(double wallNowMs)
{
    if (m_paused) return;

    for (auto it = m_buffer.begin(); it != m_buffer.end();) {
        if (wallNowMs - it->second.at.value_or(0) > PACKET_TTL_MS) {
            it = m_buffer.erase(it);
        } else {
            ++it;
        }
    }

    m_readings = m_buffer;

    double latest = 0;
    for (const auto& [key, packet] : m_buffer) {
        const double at = packet.at.value_or(0);
        if (wallNowMs - at < PACKET_TTL_MS && at > latest) latest = at;
    }
    m_lastPacketAt = latest == 0 ? std::nullopt : std::optional<double>(latest);

    m_now = SiteTime::fromEpochMs(wallNowMs);
}

TwinFrame LiveTwin::frame(PhaseId phase, double wallNowMs) const
{
    return buildFrame(phase, m_now, m_readings, wallNowMs);
}

Json LiveTwin::state(PhaseId phase, double wallNowMs) const
{
    Json j;
    j["frame"] = toJson(frame(phase, wallNowMs));
    j["paused"] = m_paused;
    j["feed"] = m_feed;
    j["endpoint"] = m_endpoint.empty() ? Json(nullptr) : Json(m_endpoint);
    j["lastPacketAt"] = m_lastPacketAt ? Json(*m_lastPacketAt) : Json(nullptr);
    return j;
}

}  // namespace dhun
