#pragma once

/* ============================================================
   NUMBER FORMATTING
   Port of lib/format.ts: Indian digit grouping, fixed precision,
   explicit units. A leading minus becomes U+2212 (−) the way the
   upstream num() rewrites it.
   ============================================================ */

#include <string>

namespace dhun {

std::string num(double value, int decimals = 0);
std::string rupees(double value);
std::string tonnes(double value);
std::string percent(double value, int decimals = 0);
std::string signedNum(double value, int decimals = 0);

}  // namespace dhun
