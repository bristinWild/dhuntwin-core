#pragma once

/* Structural JSON comparison for the parity tests: same keys in the
   same order, strings equal, numbers equal to 1e-9 relative. */

#include <cmath>
#include <iostream>
#include <string>

#include "dhun/js.h"

namespace dhun::test {

struct Report {
    int checks = 0;
    int failures = 0;
    int printed = 0;

    void fail(const std::string& path, const std::string& what)
    {
        ++failures;
        if (printed < 40) {
            std::cerr << "  MISMATCH " << path << ": " << what << "\n";
            ++printed;
        }
    }
};

inline bool closeEnough(double a, double b)
{
    if (a == b) return true;
    if (std::isnan(a) && std::isnan(b)) return true;
    const double diff = std::fabs(a - b);
    const double scale = std::max(std::fabs(a), std::fabs(b));
    return diff <= 1e-9 * scale || diff <= 1e-9;
}

inline void compare(const Json& expected, const Json& actual, const std::string& path, Report& r)
{
    ++r.checks;

    if (expected.is_number() && actual.is_number()) {
        const double e = expected.get<double>();
        const double a = actual.get<double>();
        if (!closeEnough(e, a)) {
            r.fail(path, "expected " + js::toString(e) + " got " + js::toString(a));
        }
        return;
    }

    if (expected.type() != actual.type()) {
        r.fail(path, "type differs: expected " + js::stringify(expected).substr(0, 120) +
                         " got " + js::stringify(actual).substr(0, 120));
        return;
    }

    if (expected.is_object()) {
        auto ei = expected.begin();
        auto ai = actual.begin();
        for (; ei != expected.end() && ai != actual.end(); ++ei, ++ai) {
            if (ei.key() != ai.key()) {
                r.fail(path, "key order/name: expected '" + ei.key() + "' got '" + ai.key() + "'");
                return;
            }
            compare(ei.value(), ai.value(), path + "." + ei.key(), r);
        }
        if (expected.size() != actual.size()) {
            std::string extra;
            for (auto it = actual.begin(); it != actual.end(); ++it)
                if (!expected.contains(it.key())) extra += it.key() + " ";
            std::string missing;
            for (auto it = expected.begin(); it != expected.end(); ++it)
                if (!actual.contains(it.key())) missing += it.key() + " ";
            r.fail(path, "key count differs; missing [" + missing + "] extra [" + extra + "]");
        }
        return;
    }

    if (expected.is_array()) {
        if (expected.size() != actual.size()) {
            r.fail(path, "length " + std::to_string(expected.size()) + " vs " +
                             std::to_string(actual.size()));
            return;
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            compare(expected[i], actual[i], path + "[" + std::to_string(i) + "]", r);
        }
        return;
    }

    if (expected != actual) {
        r.fail(path, "expected " + js::stringify(expected) + " got " + js::stringify(actual));
    }
}

}  // namespace dhun::test
