#pragma once

/* ============================================================
   JS-COMPATIBLE PRIMITIVES
   The engine is a port of the DhunTwin OS TypeScript engine.
   Everything the UI reads must come out byte-for-byte the way
   the browser produced it, so the handful of JavaScript built-ins
   the engine leans on are reproduced here exactly:

     Math.round            half towards +infinity, keeps -0
     String(number)        ECMAScript Number::toString
     Number#toFixed        exact binary value, ties away from zero
     Intl.NumberFormat     shortest decimal, ties away from zero,
                           en-IN grouping (1,23,45,678)
     JSON.stringify        key order preserved, JS number text
   ============================================================ */

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace dhun {

using Json = nlohmann::ordered_json;

namespace js {

/* Math.round */
double round(double value);

/* String(value) / template-literal interpolation */
std::string toString(double value);

/* value.toFixed(digits) */
std::string toFixed(double value, int digits);

/* new Intl.NumberFormat(locale, { minimumFractionDigits,
   maximumFractionDigits }).format(value). Grouping is en-IN
   (lakh/crore) when indian is true, otherwise en-US (thousands). */
std::string formatNumber(double value, int minFraction, int maxFraction, bool indian = true);

/* String.prototype.padStart(width, fill) for ASCII fills. */
std::string padStart(const std::string& text, std::size_t width, char fill);

/* String.prototype.toLowerCase for ASCII and Latin-1 / Latin
   Extended-A letters, which covers every identifier and label the
   app stores. */
std::string toLower(std::string_view text);

/* String.prototype.trim (ECMAScript white space + line terminators). */
std::string trim(std::string_view text);

bool includes(std::string_view haystack, std::string_view needle);
bool startsWith(std::string_view text, std::string_view prefix);

/* Date.prototype.toISOString for an epoch in milliseconds. */
std::string toISOString(double epochMs);

/* Date.parse for the ISO-8601 forms the app produces and accepts
   (YYYY, YYYY-MM, YYYY-MM-DD, with THH:mm[:ss[.sss]] and Z or
   +HH:mm). Date-only forms are UTC; date-time forms without an
   offset are read as site time (IST), the way the twin speaks.
   Returns NaN when the text is not one of those forms. */
double parseDate(std::string_view text);

/* parseFloat(text): longest numeric prefix, NaN when there is none. */
double parseFloat(std::string_view text);

/* JSON.stringify(value, null, indent) with ECMAScript number text.
   indent 0 means compact. */
std::string stringify(const Json& value, int indent = 0);

/* JSON.parse; throws std::runtime_error with a message close to V8's
   on malformed input. */
Json parse(std::string_view text);

/* Number(value) for a JSON value (numbers, numeric strings, booleans,
   null -> 0, otherwise NaN), as the original code coerces form input. */
double toNumber(const Json& value);

}  // namespace js
}  // namespace dhun
