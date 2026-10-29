#pragma once

/* ============================================================
   DIGITAL TWIN — LIVE LAYER
   Ports of twin/buildFrame.ts, twin/source.ts (packet parsing)
   and the frame clock of twin/useLiveTwin.ts. Frames are
   deterministic 2-second states of every instrumented asset;
   gateway packets override the model for 30 seconds.
   ============================================================ */

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "dhun/clock.h"
#include "dhun/js.h"
#include "dhun/phases.h"
#include "dhun/simulation.h"

namespace dhun {

inline constexpr int HISTORY_POINTS = 36;
inline constexpr double HISTORY_STEP_MS = 2000;
inline constexpr double PACKET_TTL_MS = 30000;
inline constexpr double FRAME_MS = 2000;

struct TelemetryPacket {
    std::string nodeId;
    std::string metric;
    double value = 0;
    std::optional<double> at;  // Unix ms
};

/* keyed "nodeId:metric" */
using PacketMap = std::map<std::string, TelemetryPacket>;

struct TwinMetric {
    std::string key;
    std::string label;
    std::string unit;
    int decimals = 0;
    double value = 0;
    std::string status;  // normal | watch | alarm
    std::array<double, 2> normal{};
    std::array<double, 2> alarm{};
    std::vector<double> history;
    bool live = false;
};

struct TwinControl {
    std::string key;
    std::string label;
    std::vector<std::string> options;
    std::string defaultValue;
    std::string note;
};

struct TwinNode {
    std::string id;
    std::string name;
    std::string shortName;
    std::string kind;
    double x = 0;
    double y = 0;

    std::string status;  // online | watch | alarm | offline
    bool available = true;
    std::optional<std::string> availabilityNote;
    int plannedChannels = 0;

    std::string deviceId;
    std::string protocol;
    std::string gateway;
    std::string description;

    std::string primaryKey;
    std::vector<TwinMetric> metrics;
    std::vector<TwinControl> controls;
};

struct TwinFrame {
    PhaseId phase = PhaseId::Today;
    SiteTime at;
    TwinSummary summary;
    std::vector<TwinNode> nodes;

    int online = 0;
    int planned = 0;
    int sensors = 0;
    int alarms = 0;
    int watches = 0;
};

Json toJson(const TwinMetric& metric);
Json toJson(const TwinControl& control);
Json toJson(const TwinNode& node);
Json toJson(const TwinFrame& frame);

/* buildFrame(phase, at, packets); wallNowMs is Date.now(). */
TwinFrame buildFrame(PhaseId phase, const SiteTime& at, const PacketMap& packets, double wallNowMs);

/* TwinEvent[] (at most eight, alarms first) */
Json buildEvents(const TwinFrame& frame);

/* TwinAction[]: compute deferral, pump window, water gap */
Json buildActions(PhaseId phase, const SiteTime& at);

const TwinMetric* primaryMetric(const TwinNode& node);

/* TWIN_STATUS / METRIC_STATUS label + colour tables */
const Json& twinStatusTable();

/* source.ts parsePacket: one packet, an array, or a
   { packets: [...] } envelope. Malformed input yields nothing. */
std::vector<TelemetryPacket> parsePacket(const std::string& raw, double wallNowMs);

Json toJson(const TelemetryPacket& packet);

/* ------------------------------------------------------------
   LIVE TWIN — the frame clock of useLiveTwin.ts
   Two-second frames; pausing freezes the clock (and the packet
   snapshot) while the gateway keeps delivering into the buffer.
   ------------------------------------------------------------ */

class LiveTwin {
public:
    explicit LiveTwin(double wallNowMs);

    /* A packet from the gateway (useLiveTwin's onPacket). */
    void receive(const TelemetryPacket& packet);

    /* One frame tick: prune expired packets, snapshot the buffer,
       advance the clock. A paused twin ignores ticks. */
    void tick(double wallNowMs);

    void setPaused(bool paused) { m_paused = paused; }
    bool paused() const { return m_paused; }

    /* simulated | connecting | live | stale | offline */
    void setFeed(const std::string& feed) { m_feed = feed; }
    const std::string& feed() const { return m_feed; }

    void setEndpoint(const std::string& endpoint) { m_endpoint = endpoint; }
    const std::string& endpoint() const { return m_endpoint; }

    std::optional<double> lastPacketAt() const { return m_lastPacketAt; }
    const SiteTime& now() const { return m_now; }

    TwinFrame frame(PhaseId phase, double wallNowMs) const;

    /* { frame, paused, feed, endpoint, lastPacketAt } */
    Json state(PhaseId phase, double wallNowMs) const;

private:
    PacketMap m_buffer;    // packets.current
    PacketMap m_readings;  // the snapshot the frame is built from
    SiteTime m_now;
    bool m_paused = false;
    std::string m_feed = "simulated";
    std::string m_endpoint;
    std::optional<double> m_lastPacketAt;
};

}  // namespace dhun
