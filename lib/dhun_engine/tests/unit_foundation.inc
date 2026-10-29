/* Foundation: compiled constants and phases match the upstream
   literals, the catalog is complete, JS-compatible helpers behave. */

#include <fstream>

#include "dhun/catalog.h"
#include "dhun/clock.h"
#include "dhun/constants.h"
#include "dhun/format.h"
#include "dhun/phases.h"

namespace {

dhun::Json readData(const std::string& name)
{
    std::ifstream in(std::string(DHUN_DATA_DIR) + "/" + name + ".json");
    return dhun::Json::parse(in);
}

}  // namespace

TEST("constants match upstream constants.ts")
{
    const auto c = readData("constants");
    using namespace dhun;
#define CONST(name) CHECK_EQ(c[#name].get<double>(), name)
    CONST(TOTAL_SITE_ACRES); CONST(BIORESERVE_ACRES); CONST(MAX_CONSTRUCTION_ACRES);
    CONST(TREE_HEIGHT_LIMIT_M); CONST(AFFORESTATION_OFFSET_ACRES); CONST(ANNUAL_WATER_CAPACITY_KL);
    CONST(GRID_FACTOR); CONST(SOLAR_FACTOR); CONST(PETROL_FACTOR); CONST(DIESEL_FACTOR);
    CONST(LPG_FACTOR); CONST(SPECIFIC_YIELD); CONST(SOLAR_PEAK_FACTOR); CONST(SOILING_FACTOR);
    CONST(EPI_TARGET); CONST(EPI_EFFICIENT); CONST(EPI_BASELINE); CONST(LITRES_PER_PERSON_DAY);
    CONST(LANDSCAPE_LITRES_M2_DAY); CONST(STP_ENERGY_KWH_PER_KL); CONST(PUMP_ENERGY_KWH_PER_KL);
    CONST(DESIGN_POPULATION); CONST(TARGET_PER_CAPITA_T); CONST(UPFRONT_KG_M2);
    CONST(USE_EMBODIED_KG_M2); CONST(BUILD_OUT_M2); CONST(STANDING_STOCK_T);
    CONST(BIO_SEQUESTRATION_T_YR); CONST(FARM_SEQUESTRATION_T_YR); CONST(TOTAL_SEQUESTRATION_T_YR);
    CONST(SELF_CONSUMPTION_FACTOR); CONST(PV_CAPEX_INR_PER_KWP); CONST(BATTERY_CAPEX_INR_PER_KWH);
    CONST(BATTERY_ROUND_TRIP_EFFICIENCY); CONST(SYSTEM_LIFETIME_YR); CONST(OANDM_FRACTION);
    CONST(TARIFF_INR_PER_KWH); CONST(GREEN_LAND_M2); CONST(LANDSCAPE_KL_YR_BUILD_OUT);
#undef CONST
    /* every numeric upstream constant is accounted for */
    int numeric = 0;
    for (auto it = c.begin(); it != c.end(); ++it) numeric += it.value().is_number() ? 1 : 0;
    CHECK_EQ(numeric, 39);
}

TEST("phases match upstream phases.ts")
{
    const auto p = readData("phases");
    using namespace dhun;
    for (PhaseId id : PHASE_ORDER) {
        const auto& expected = p["PHASES"][std::string(phaseKey(id))];
        CHECK(expected == phaseToJson(phaseConfig(id)));
        CHECK_EQ(p["PHASE_START_YEAR"][std::string(phaseKey(id))].get<int>(), phaseStartYear(id));
    }
    for (std::size_t i = 0; i < 12; ++i) CHECK_EQ(p["MONTH_FACTOR"][i].get<double>(), MONTH_FACTOR[i]);
    CHECK_EQ(p["PHASE_ORDER"].size(), PHASE_ORDER.size());
}

TEST("catalog embeds every extracted dataset")
{
    const auto names = dhun::catalog::names();
    CHECK(names.size() >= 13);
    for (const auto& name : names) {
        CHECK(readData(name) == dhun::catalog::get(name));
    }
}

TEST("formatting helpers mirror lib/format.ts")
{
    using namespace dhun;
    CHECK_EQ(num(1234567.4), std::string("12,34,567"));
    CHECK_EQ(num(-12.5, 1), std::string("−12.5"));
    CHECK_EQ(num(-0.2), std::string("−0"));
    CHECK_EQ(rupees(250000.4), std::string("₹2,50,000"));
    CHECK_EQ(tonnes(146400), std::string("146k"));
    CHECK_EQ(tonnes(14640), std::string("14.6k"));
    CHECK_EQ(tonnes(640.4), std::string("640"));
    CHECK_EQ(percent(0.4567), std::string("46%"));
    CHECK_EQ(signedNum(3.21, 1), std::string("+3.2"));
    CHECK_EQ(signedNum(-3.21, 1), std::string("−3.2"));
}

TEST("site clock speaks IST")
{
    using namespace dhun;
    /* 2026-10-29T15:39:05.007Z is 21:09:05 in Jaipur */
    const auto t = SiteTime::fromEpochMs(1793288345007.0);
    CHECK_EQ(t.hours, 21);
    CHECK_EQ(t.minutes, 9);
    CHECK_EQ(t.seconds, 5);
    CHECK_EQ(t.month, 9);
    CHECK_EQ(istClock(t), std::string("21:09:05"));
    CHECK_EQ(istDate(t), std::string("29 Oct 2026"));
    CHECK_EQ(siteIso(t), std::string("2026-10-29T21:09:05.007+05:30"));
    /* 18:35 UTC is past midnight in IST */
    const auto late = SiteTime::fromEpochMs(1793212509000.0);
    CHECK_EQ(istTime(late), std::string("00:05"));
}

TEST("js helpers")
{
    using namespace dhun;
    CHECK_EQ(js::toString(0.1 + 0.2), std::string("0.30000000000000004"));
    CHECK_EQ(js::toString(1e21), std::string("1e+21"));
    CHECK_EQ(js::toFixed(1.005, 2), std::string("1.00"));
    CHECK_EQ(js::formatNumber(1.005, 2, 2), std::string("1.01"));
    CHECK_EQ(js::round(-2.5), -2.0);
    CHECK(std::signbit(js::round(-0.4)));
    CHECK_EQ(js::stringify(Json::parse(R"({"b":1.0,"a":[1,2.5,"x\n"]})"), 2),
             std::string("{\n  \"b\": 1,\n  \"a\": [\n    1,\n    2.5,\n    \"x\\n\"\n  ]\n}"));
    CHECK(std::isnan(js::parseDate("not a date")));
    CHECK_EQ(js::parseDate("2026-10-29T10:00:00Z"), 1793268000000.0);
    CHECK_EQ(js::parseFloat(" 12.5kW"), 12.5);
}
