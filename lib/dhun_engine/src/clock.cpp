#include "dhun/clock.h"

#include <chrono>
#include <cmath>
#include <cstdio>

namespace dhun {

namespace {

void civilFromDays(int64_t z, int& y, int& m, int& d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t year = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year += m <= 2;
    y = static_cast<int>(year);
}

const char* const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

}  // namespace

SiteTime SiteTime::fromEpochMs(double epochMs)
{
    SiteTime t;
    t.epochMs = epochMs;

    const auto local = static_cast<int64_t>(std::floor(epochMs)) +
                       static_cast<int64_t>(IST_OFFSET_MINUTES) * 60000;
    int64_t days = local / 86400000;
    int64_t rem = local % 86400000;
    if (rem < 0) {
        rem += 86400000;
        days -= 1;
    }

    int y = 0, m = 0, d = 0;
    civilFromDays(days, y, m, d);
    t.year = y;
    t.month = m - 1;
    t.day = d;
    t.hours = static_cast<int>(rem / 3600000);
    t.minutes = static_cast<int>((rem / 60000) % 60);
    t.seconds = static_cast<int>((rem / 1000) % 60);
    t.millis = static_cast<int>(rem % 1000);
    return t;
}

double wallClockMs()
{
    using namespace std::chrono;
    return static_cast<double>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

double dayFraction(const SiteTime& at)
{
    return (at.hours * 3600 + at.minutes * 60 + at.seconds) / 86400.0;
}

std::string siteIso(const SiteTime& at)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d+05:30", at.year,
                  at.month + 1, at.day, at.hours, at.minutes, at.seconds, at.millis);
    return buf;
}

std::string istTime(const SiteTime& at)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", at.hours, at.minutes);
    return buf;
}

std::string istClock(const SiteTime& at)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", at.hours, at.minutes, at.seconds);
    return buf;
}

std::string istDate(const SiteTime& at)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d %s %d", at.day, MONTHS[at.month], at.year);
    return buf;
}

}  // namespace dhun
