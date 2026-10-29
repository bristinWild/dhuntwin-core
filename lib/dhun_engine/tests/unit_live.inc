/* Live twin frame clock (useLiveTwin.ts) and registry persistence:
   the stateful parts the parity goldens cannot replay. */

#include <filesystem>

#include "dhun/registry.h"
#include "dhun/twin_frame.h"

TEST("live twin: packets override the model for 30 s, then the model returns")
{
    using namespace dhun;
    const double t0 = 1793268000000.0;
    LiveTwin live(t0);
    live.receive({"grid-hub", "import", 318.4, t0});
    live.tick(t0 + 2000);

    auto importOf = [](const TwinFrame& f) {
        for (const auto& n : f.nodes)
            for (const auto& m : n.metrics)
                if (n.id == "grid-hub" && m.key == "import") return m;
        return TwinMetric{};
    };

    TwinMetric m = importOf(live.frame(PhaseId::Today, t0 + 2000));
    CHECK(m.live);
    CHECK_EQ(m.value, 318.4);
    CHECK(live.lastPacketAt().has_value() && *live.lastPacketAt() == t0);

    live.tick(t0 + 31000);  /* past the TTL: pruned */
    m = importOf(live.frame(PhaseId::Today, t0 + 31000));
    CHECK(!m.live);
    CHECK(!live.lastPacketAt().has_value());
}

TEST("live twin: a paused clock freezes the frame and the packet snapshot")
{
    using namespace dhun;
    const double t0 = 1793268000000.0;
    LiveTwin live(t0);
    live.tick(t0);
    const double frozenAt = live.now().epochMs;

    live.setPaused(true);
    live.receive({"stp", "ph", 9.9, t0 + 1000});
    live.tick(t0 + 4000);
    CHECK_EQ(live.now().epochMs, frozenAt);
    CHECK(!live.lastPacketAt().has_value());

    live.setPaused(false);
    live.tick(t0 + 6000);
    CHECK_EQ(live.now().epochMs, t0 + 6000);
    CHECK(live.lastPacketAt().has_value());
}

TEST("registry: file storage survives a restart, like localStorage")
{
    using namespace dhun;
    const auto dir = std::filesystem::temp_directory_path() / "dhun_registry_unit";
    std::filesystem::remove_all(dir);
    const std::string path = (dir / "asset_registry.json").string();

    {
        AssetRegistry reg(fileStorage(path));
        CHECK_EQ(reg.assets().size(), initialSeedAssets().size());
        const Json asset = createAssetFromTemplate(Json("WATER_METER_PUMP"),
                                                   Json::parse(R"({"id":"pump-9","name":"Pump 9"})"), 0);
        CHECK(reg.addAsset(asset).success);
    }
    {
        AssetRegistry reg(fileStorage(path));
        CHECK_EQ(reg.assets().size(), initialSeedAssets().size() + 1);
        CHECK(reg.getAssetById("PUMP-9").has_value());
    }
    std::filesystem::remove_all(dir);
}
