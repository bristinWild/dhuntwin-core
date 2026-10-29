#pragma once

/* Small helpers for reading TS-style option bags from JSON with the
   same semantics the upstream code relies on. */

#include <limits>
#include <optional>
#include <string>

#include "dhun/js.h"

namespace dhun {

/* `obj.key !== undefined` (present, possibly null) */
inline bool defined(const Json& obj, const char* key)
{
    return obj.is_object() && obj.contains(key);
}

/* `obj.key ?? fallback` for numbers: absent or null -> fallback,
   otherwise Number(value). */
inline double numberOr(const Json& obj, const char* key, double fallback)
{
    if (!obj.is_object()) return fallback;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return fallback;
    return js::toNumber(*it);
}

/* Number(obj.key): absent -> NaN (Number(undefined)). */
inline double numberOf(const Json& obj, const char* key)
{
    if (!obj.is_object()) return std::numeric_limits<double>::quiet_NaN();
    auto it = obj.find(key);
    if (it == obj.end()) return std::numeric_limits<double>::quiet_NaN();
    return js::toNumber(*it);
}

/* String value or fallback when absent/not a string. */
inline std::string stringOr(const Json& obj, const char* key, const std::string& fallback = "")
{
    if (!obj.is_object()) return fallback;
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

/* JS truthiness of obj.key. */
inline bool truthy(const Json& value)
{
    switch (value.type()) {
    case Json::value_t::null:
    case Json::value_t::discarded:
        return false;
    case Json::value_t::boolean:
        return value.get<bool>();
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float: {
        const double d = value.get<double>();
        return d != 0 && d == d;
    }
    case Json::value_t::string:
        return !value.get_ref<const std::string&>().empty();
    default:
        return true;  // objects, arrays
    }
}

inline bool truthyKey(const Json& obj, const char* key)
{
    if (!obj.is_object()) return false;
    auto it = obj.find(key);
    return it != obj.end() && truthy(*it);
}

/* Nullable number -> JSON (null when absent). */
inline Json nullable(const std::optional<double>& value)
{
    return value ? Json(*value) : Json(nullptr);
}

}  // namespace dhun
