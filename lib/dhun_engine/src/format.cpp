#include "dhun/format.h"

#include <cmath>

#include "dhun/js.h"

namespace dhun {

std::string num(double value, int decimals)
{
    std::string text = js::formatNumber(value, decimals, decimals);

    /* String#replace with a string pattern rewrites the first match. */
    const auto pos = text.find('-');
    if (pos != std::string::npos) text.replace(pos, 1, "−");
    return text;
}

std::string rupees(double value)
{
    /* new Intl.NumberFormat("en-IN").format(...) defaults to at most
       three fraction digits; Math.round leaves none to show. */
    return "₹" + js::formatNumber(js::round(value), 0, 3);
}

std::string tonnes(double value)
{
    const double abs = std::fabs(value);

    if (abs >= 100000) return num(value / 1000, 0) + "k";
    if (abs >= 1000) return num(value / 1000, 1) + "k";
    return num(value, 0);
}

std::string percent(double value, int decimals)
{
    return num(value * 100, decimals) + "%";
}

std::string signedNum(double value, int decimals)
{
    const std::string prefix = value > 0 ? "+" : "";
    return prefix + num(value, decimals);
}

}  // namespace dhun
