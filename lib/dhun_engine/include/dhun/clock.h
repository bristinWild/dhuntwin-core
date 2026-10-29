#pragma once

/* ============================================================
   CLOCK
   Port of lib/clock.ts. The site is in Rajasthan and the twin
   always speaks IST, whatever timezone the host runs in. The
   upstream app shifts a Date by the laptop's offset and reads
   local getters; here the engine takes the true instant and
   derives IST wall-clock fields directly — the same numbers.
   ============================================================ */

#include <cstdint>
#include <string>

namespace dhun {

inline constexpr int IST_OFFSET_MINUTES = 330;

struct SiteTime {
    double epochMs = 0;  // the true instant (Date.now())
    int year = 1970;
    int month = 0;       // 0-11, Date#getMonth
    int day = 1;
    int hours = 0;
    int minutes = 0;
    int seconds = 0;
    int millis = 0;

    static SiteTime fromEpochMs(double epochMs);

    /* Date#getTime of the instant. */
    double getTime() const { return epochMs; }
};

/* Wall-clock milliseconds since epoch (Date.now()). */
double wallClockMs();

/* Fraction of the IST day elapsed: (h*3600 + m*60 + s) / 86400. */
double dayFraction(const SiteTime& at);

/* "2026-10-29T21:09:05.000+05:30" — the instant in site time. */
std::string siteIso(const SiteTime& at);

/* "21:09" and "21:09:05" (istTime / istClock, en-IN 24h). */
std::string istTime(const SiteTime& at);
std::string istClock(const SiteTime& at);

/* "29 Oct 2026" (istDate, en-IN). */
std::string istDate(const SiteTime& at);

}  // namespace dhun
