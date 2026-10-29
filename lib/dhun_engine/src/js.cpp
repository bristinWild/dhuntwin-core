#include "dhun/js.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <vector>

/* Floating-point <charconv> needs macOS 13.3+ in libc++; Basecamp's macOS
   builds may target older systems. There, the same results come from the C
   library under a pinned "C" locale (so LC_NUMERIC set by a Qt host cannot
   turn the decimal point into a comma). */
#if defined(__APPLE__) || defined(DHUN_NO_FLOAT_CHARCONV)
#define DHUN_FLOAT_CHARCONV 0
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
#include <cstdlib>
#else
#define DHUN_FLOAT_CHARCONV 1
#endif

namespace dhun::js {

namespace {

#if !DHUN_FLOAT_CHARCONV
/* Runs fn with the calling thread switched to the "C" locale. */
template <typename F>
auto inCLocale(F&& fn)
{
    static locale_t c = newlocale(LC_ALL_MASK, "C", static_cast<locale_t>(nullptr));
    locale_t previous = uselocale(c);
    auto result = fn();
    uselocale(previous);
    return result;
}
#endif

/* Shortest round-trip scientific text of a finite positive double,
   "d.ddde+XX" (the shape std::to_chars(scientific) produces). */
std::string scientificShortest(double value)
{
#if DHUN_FLOAT_CHARCONV
    std::array<char, 64> buf{};
    auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::scientific);
    return std::string(buf.data(), res.ptr);
#else
    return inCLocale([value] {
        char buf[64];
        for (int precision = 0; precision < 17; ++precision) {
            std::snprintf(buf, sizeof buf, "%.*e", precision, value);
            if (std::strtod(buf, nullptr) == value) return std::string(buf);
        }
        std::snprintf(buf, sizeof buf, "%.16e", value);
        return std::string(buf);
    });
#endif
}

/* Exact fixed-point text with `digits` fraction digits. */
std::string fixedExact(double value, int digits)
{
#if DHUN_FLOAT_CHARCONV
    std::vector<char> buf(1500);
    auto res = std::to_chars(buf.data(), buf.data() + buf.size(), value, std::chars_format::fixed, digits);
    return std::string(buf.data(), res.ptr);
#else
    return inCLocale([value, digits] {
        std::vector<char> buf(1500);
        std::snprintf(buf.data(), buf.size(), "%.*f", digits, value);
        return std::string(buf.data());
    });
#endif
}

double decimalToDouble(const std::string& number)
{
#if DHUN_FLOAT_CHARCONV
    double value = 0;
    std::from_chars(number.data(), number.data() + number.size(), value);
    return value;
#else
    return inCLocale([&number] { return std::strtod(number.c_str(), nullptr); });
#endif
}

/* Shortest round-trip decimal digits of a finite, positive double:
   value = 0.d1d2...dk × 10^n (ECMAScript Number::toString terms). */
struct Shortest {
    std::string digits;  // no leading/trailing zeros, at least one digit
    int n = 0;           // decimal point position
};

Shortest shortest(double value)
{
    std::string text = scientificShortest(value);

    const auto e = text.find('e');
    std::string mantissa = text.substr(0, e);
    const int exponent = std::stoi(text.substr(e + 1));

    Shortest out;
    for (char c : mantissa) {
        if (c != '.') out.digits.push_back(c);
    }
    while (out.digits.size() > 1 && out.digits.back() == '0') {
        out.digits.pop_back();
    }
    out.n = exponent + 1;
    return out;
}

void group(std::string& integer, bool indian)
{
    if (integer.size() <= 3) return;

    std::string out;
    const std::size_t len = integer.size();
    std::string head = integer.substr(0, len - 3);
    const std::string tail = integer.substr(len - 3);
    const std::size_t step = indian ? 2 : 3;

    std::vector<std::string> parts;
    while (head.size() > step) {
        parts.insert(parts.begin(), head.substr(head.size() - step));
        head.erase(head.size() - step);
    }
    if (!head.empty()) parts.insert(parts.begin(), head);

    for (const auto& part : parts) {
        out += part;
        out += ',';
    }
    out += tail;
    integer = out;
}

/* Round a non-negative decimal "int.frac" string to `digits` fraction
   digits, ties (and above) away from zero. */
void roundDecimal(std::string& integer, std::string& fraction, int digits)
{
    if (static_cast<int>(fraction.size()) <= digits) {
        fraction.append(static_cast<std::size_t>(digits) - fraction.size(), '0');
        return;
    }

    const bool up = fraction[static_cast<std::size_t>(digits)] >= '5';
    fraction.resize(static_cast<std::size_t>(digits));

    if (!up) return;

    for (int i = digits - 1; i >= 0; --i) {
        auto& c = fraction[static_cast<std::size_t>(i)];
        if (c == '9') {
            c = '0';
        } else {
            ++c;
            return;
        }
    }

    for (int i = static_cast<int>(integer.size()) - 1; i >= 0; --i) {
        auto& c = integer[static_cast<std::size_t>(i)];
        if (c == '9') {
            c = '0';
        } else {
            ++c;
            return;
        }
    }
    integer.insert(integer.begin(), '1');
}

void appendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

/* Decode one UTF-8 code point at i (advancing i). Invalid bytes are
   passed through as themselves. */
uint32_t decodeUtf8(std::string_view text, std::size_t& i)
{
    const auto b0 = static_cast<unsigned char>(text[i]);
    auto cont = [&](std::size_t k) -> int {
        if (i + k >= text.size()) return -1;
        const auto b = static_cast<unsigned char>(text[i + k]);
        return (b & 0xC0) == 0x80 ? (b & 0x3F) : -1;
    };

    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        const int c1 = cont(1);
        if (c1 >= 0) {
            i += 2;
            return (static_cast<uint32_t>(b0 & 0x1F) << 6) | static_cast<uint32_t>(c1);
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        const int c1 = cont(1), c2 = cont(2);
        if (c1 >= 0 && c2 >= 0) {
            i += 3;
            return (static_cast<uint32_t>(b0 & 0x0F) << 12) |
                   (static_cast<uint32_t>(c1) << 6) | static_cast<uint32_t>(c2);
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        const int c1 = cont(1), c2 = cont(2), c3 = cont(3);
        if (c1 >= 0 && c2 >= 0 && c3 >= 0) {
            i += 4;
            return (static_cast<uint32_t>(b0 & 0x07) << 18) |
                   (static_cast<uint32_t>(c1) << 12) |
                   (static_cast<uint32_t>(c2) << 6) | static_cast<uint32_t>(c3);
        }
    }
    ++i;
    return 0xFFFFFFFFu;  // marker: raw byte b0
}

bool isJsSpace(uint32_t cp)
{
    switch (cp) {
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
    case 0xA0: case 0x1680: case 0x2028: case 0x2029: case 0x202F:
    case 0x205F: case 0x3000: case 0xFEFF:
        return true;
    default:
        return cp >= 0x2000 && cp <= 0x200A;
    }
}

/* Days from civil (Howard Hinnant's algorithm). */
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

void quote(std::string& out, const std::string& text)
{
    out.push_back('"');
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(ch);
            }
        }
    }
    out.push_back('"');
}

void write(std::string& out, const Json& value, int indent, int depth)
{
    const std::string pad = indent > 0 ? std::string(static_cast<std::size_t>(indent * (depth + 1)), ' ') : "";
    const std::string closePad = indent > 0 ? std::string(static_cast<std::size_t>(indent * depth), ' ') : "";

    switch (value.type()) {
    case Json::value_t::null:
    case Json::value_t::discarded:
        out += "null";
        return;
    case Json::value_t::boolean:
        out += value.get<bool>() ? "true" : "false";
        return;
    case Json::value_t::number_integer:
        out += std::to_string(value.get<int64_t>());
        return;
    case Json::value_t::number_unsigned:
        out += std::to_string(value.get<uint64_t>());
        return;
    case Json::value_t::number_float: {
        const double d = value.get<double>();
        out += std::isfinite(d) ? toString(d) : "null";
        return;
    }
    case Json::value_t::string:
        quote(out, value.get_ref<const std::string&>());
        return;
    case Json::value_t::binary:
        out += "null";
        return;
    case Json::value_t::array: {
        if (value.empty()) {
            out += "[]";
            return;
        }
        out.push_back('[');
        bool first = true;
        for (const auto& item : value) {
            if (!first) out.push_back(',');
            first = false;
            if (indent > 0) {
                out.push_back('\n');
                out += pad;
            }
            write(out, item, indent, depth + 1);
        }
        if (indent > 0) {
            out.push_back('\n');
            out += closePad;
        }
        out.push_back(']');
        return;
    }
    case Json::value_t::object: {
        if (value.empty()) {
            out += "{}";
            return;
        }
        out.push_back('{');
        bool first = true;
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!first) out.push_back(',');
            first = false;
            if (indent > 0) {
                out.push_back('\n');
                out += pad;
            }
            quote(out, it.key());
            out += indent > 0 ? ": " : ":";
            write(out, it.value(), indent, depth + 1);
        }
        if (indent > 0) {
            out.push_back('\n');
            out += closePad;
        }
        out.push_back('}');
        return;
    }
    }
}


/* ------------------------------------------------------------
   V8-style JSON.parse error messages. Only consulted when the
   parser has already rejected the text: walks the grammar the way
   V8's JsonParser does and reports the first problem with the
   same wording and position.
   ------------------------------------------------------------ */

class SyntaxCheck {
public:
    explicit SyntaxCheck(std::string_view text) : t(text) {}

    /* empty when the text is valid JSON */
    std::string run()
    {
        try {
            ws();
            value();
            ws();
            if (i < t.size()) fail("Unexpected non-whitespace character after JSON", i);
        } catch (const std::string& message) {
            return message;
        }
        return {};
    }

private:
    std::string_view t;
    std::size_t i = 0;

    [[noreturn]] void fail(const std::string& what, std::size_t pos)
    {
        std::size_t line = 1, col = 1;
        for (std::size_t k = 0; k < pos && k < t.size(); ++k) {
            if (t[k] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        const bool trailing = what.rfind("after JSON") != std::string::npos;
        throw what + (trailing ? " at position " : " in JSON at position ") + std::to_string(pos) + " (line " + std::to_string(line) +
            " column " + std::to_string(col) + ")";
    }

    [[noreturn]] void unexpected(std::size_t pos)
    {
        if (pos >= t.size()) throw std::string("Unexpected end of JSON input");
        std::string snippet;
        if (t.size() <= 20) {
            snippet = "\"" + std::string(t) + "\"";
        } else {
            const std::size_t start = pos >= 10 ? pos - 10 : 0;
            const std::size_t end = std::min(t.size(), pos + 10);
            snippet = (start > 0 ? "...\"" : "\"") + std::string(t.substr(start, end - start)) +
                      (end < t.size() ? "\"..." : "\"");
        }
        throw "Unexpected token '" + std::string(1, t[pos]) + "', " + snippet + " is not valid JSON";
    }

    void ws()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
    }

    void literal(const char* word)
    {
        for (const char* c = word; *c; ++c, ++i) {
            if (i >= t.size() || t[i] != *c) unexpected(i);
        }
    }

    bool digit(std::size_t k) const { return k < t.size() && t[k] >= '0' && t[k] <= '9'; }

    void number()
    {
        if (t[i] == '-') {
            ++i;
            if (!digit(i)) fail("No number after minus sign", i);
        }
        if (t[i] == '0') {
            ++i;
            if (digit(i)) fail("Unexpected number", i);
        } else {
            while (digit(i)) ++i;
        }
        if (i < t.size() && t[i] == '.') {
            ++i;
            if (!digit(i)) fail("Unterminated fractional number", i);
            while (digit(i)) ++i;
        }
        if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) {
            ++i;
            if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
            if (!digit(i)) fail("Exponent part is missing a number", i);
            while (digit(i)) ++i;
        }
    }

    void string()
    {
        ++i;  // opening quote
        while (true) {
            if (i >= t.size()) fail("Unterminated string", i);
            const auto c = static_cast<unsigned char>(t[i]);
            if (c == '"') {
                ++i;
                return;
            }
            if (c < 0x20) fail("Bad control character in string literal", i);
            if (c == '\\') {
                ++i;
                if (i >= t.size()) fail("Unterminated string", i);
                const char e = t[i];
                if (e == 'u') {
                    ++i;
                    for (int k = 0; k < 4; ++k, ++i) {
                        if (i >= t.size()) fail("Unterminated string", i);
                        const char h = t[i];
                        const bool hex = (h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F');
                        if (!hex) fail("Bad Unicode escape", i);
                    }
                    continue;
                }
                if (std::string_view("\"\\/bfnrt").find(e) == std::string_view::npos) {
                    fail("Bad escaped character", i);
                }
            }
            ++i;
        }
    }

    void value()
    {
        if (i >= t.size()) unexpected(i);
        const char c = t[i];
        switch (c) {
        case '{': object(); return;
        case '[': array(); return;
        case '"': string(); return;
        case 't': literal("true"); return;
        case 'f': literal("false"); return;
        case 'n': literal("null"); return;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) {
                number();
                return;
            }
            unexpected(i);
        }
    }

    void array()
    {
        ++i;
        ws();
        if (i < t.size() && t[i] == ']') {
            ++i;
            return;
        }
        while (true) {
            ws();
            value();
            ws();
            if (i >= t.size()) unexpected(i);
            if (t[i] == ',') {
                ++i;
                continue;
            }
            if (t[i] == ']') {
                ++i;
                return;
            }
            fail("Expected ',' or ']' after array element", i);
        }
    }

    void object()
    {
        ++i;
        ws();
        if (i >= t.size()) unexpected(i);
        if (t[i] == '}') {
            ++i;
            return;
        }
        if (t[i] != '"') fail("Expected property name or '}'", i);
        while (true) {
            string();
            ws();
            if (i >= t.size()) unexpected(i);
            if (t[i] != ':') fail("Expected ':' after property name", i);
            ++i;
            ws();
            value();
            ws();
            if (i >= t.size()) unexpected(i);
            if (t[i] == '}') {
                ++i;
                return;
            }
            if (t[i] != ',') fail("Expected ',' or '}' after property value", i);
            ++i;
            ws();
            if (i >= t.size()) unexpected(i);
            if (t[i] != '"') fail("Expected double-quoted property name", i);
        }
    }
};

}  // namespace

double parseFloat(std::string_view raw)
{
    const std::string text = trim(raw);
    std::size_t k = 0;
    bool negative = false;
    if (k < text.size() && (text[k] == '+' || text[k] == '-')) {
        negative = text[k] == '-';
        ++k;
    }
    if (text.compare(k, 8, "Infinity") == 0) {
        return negative ? -std::numeric_limits<double>::infinity()
                        : std::numeric_limits<double>::infinity();
    }

    /* longest StrDecimalLiteral prefix */
    const std::size_t start = k;
    bool any = false;
    while (k < text.size() && text[k] >= '0' && text[k] <= '9') { ++k; any = true; }
    if (k < text.size() && text[k] == '.') {
        std::size_t j = k + 1;
        bool frac = false;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') { ++j; frac = true; }
        if (frac || any) { k = j; any = any || frac; }
    }
    if (!any) return std::numeric_limits<double>::quiet_NaN();
    if (k < text.size() && (text[k] == 'e' || text[k] == 'E')) {
        std::size_t j = k + 1;
        if (j < text.size() && (text[j] == '+' || text[j] == '-')) ++j;
        bool exp = false;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9') { ++j; exp = true; }
        if (exp) k = j;
    }

    std::string number = text.substr(start, k - start);
    if (!number.empty() && number.back() == '.') number.pop_back();
    if (!number.empty() && number.front() == '.') number.insert(number.begin(), '0');
    const double value = decimalToDouble(number);
    return negative ? -value : value;
}

double round(double value)
{
    if (!std::isfinite(value) || value == 0) return value;
    double r = std::floor(value);
    if (value - r >= 0.5) r += 1;
    if (r == 0 && value < 0) return -0.0;
    return r;
}

std::string toString(double value)
{
    if (std::isnan(value)) return "NaN";
    if (value == 0) return "0";
    if (std::isinf(value)) return value < 0 ? "-Infinity" : "Infinity";

    const bool negative = value < 0;
    const Shortest s = shortest(std::fabs(value));
    const int k = static_cast<int>(s.digits.size());
    const int n = s.n;

    std::string out;
    if (k <= n && n <= 21) {
        out = s.digits + std::string(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out = s.digits.substr(0, static_cast<std::size_t>(n)) + "." +
              s.digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        out = "0." + std::string(static_cast<std::size_t>(-n), '0') + s.digits;
    } else {
        const int e = n - 1;
        out = s.digits.substr(0, 1);
        if (k > 1) out += "." + s.digits.substr(1);
        out += "e";
        out += e >= 0 ? "+" : "-";
        out += std::to_string(e >= 0 ? e : -e);
    }
    return negative ? "-" + out : out;
}

std::string toFixed(double value, int digits)
{
    if (std::isnan(value)) return "NaN";
    if (std::fabs(value) >= 1e21) return toString(value);

    const bool negative = value < 0;
    const double x = std::fabs(value);

    /* Exact decimal expansion of the binary value. */
    /* exact: every fraction digit of the binary value */
    std::string text = fixedExact(x, 1100);

    const auto dot = text.find('.');
    std::string integer = text.substr(0, dot);
    std::string fraction = dot == std::string::npos ? "" : text.substr(dot + 1);

    roundDecimal(integer, fraction, digits);

    std::string out = integer;
    if (digits > 0) out += "." + fraction;
    return negative ? "-" + out : out;
}

std::string formatNumber(double value, int minFraction, int maxFraction, bool indian)
{
    if (std::isnan(value)) return "NaN";

    const bool negative = std::signbit(value);
    if (std::isinf(value)) return negative ? "-∞" : "∞";

    std::string integer = "0";
    std::string fraction;

    const double x = std::fabs(value);
    if (x != 0) {
        const Shortest s = shortest(x);
        const int k = static_cast<int>(s.digits.size());
        if (s.n <= 0) {
            integer = "0";
            fraction = std::string(static_cast<std::size_t>(-s.n), '0') + s.digits;
        } else if (s.n >= k) {
            integer = s.digits + std::string(static_cast<std::size_t>(s.n - k), '0');
        } else {
            integer = s.digits.substr(0, static_cast<std::size_t>(s.n));
            fraction = s.digits.substr(static_cast<std::size_t>(s.n));
        }
    }

    roundDecimal(integer, fraction, maxFraction);

    while (static_cast<int>(fraction.size()) > minFraction && !fraction.empty() &&
           fraction.back() == '0') {
        fraction.pop_back();
    }

    group(integer, indian);

    std::string out = integer;
    if (!fraction.empty()) out += "." + fraction;
    return negative ? "-" + out : out;
}

std::size_t utf16Length(std::string_view text)
{
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = decodeUtf8(text, i);
        units += (cp != 0xFFFFFFFFu && cp >= 0x10000) ? 2 : 1;
    }
    return units;
}

std::string padStart(const std::string& text, std::size_t width, char fill)
{
    if (text.size() >= width) return text;
    return std::string(width - text.size(), fill) + text;
}

std::string toLower(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t start = i;
        const uint32_t cp = decodeUtf8(text, i);
        if (cp == 0xFFFFFFFFu) {
            out.push_back(text[start]);
            continue;
        }
        uint32_t lower = cp;
        if (cp >= 'A' && cp <= 'Z') {
            lower = cp + 32;
        } else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) {
            lower = cp + 32;
        } else if (cp >= 0x100 && cp <= 0x17F && cp != 0x130 && cp != 0x131 &&
                   cp != 0x138 && cp != 0x149 && cp != 0x178 && cp != 0x17F) {
            const bool oddPairs = (cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E);
            const bool isUpper = oddPairs ? (cp % 2 == 1) : (cp % 2 == 0);
            if (isUpper) lower = cp + 1;
        } else if (cp == 0x178) {
            lower = 0xFF;
        } else if (cp >= 0x391 && cp <= 0x3A9 && cp != 0x3A2) {
            lower = cp + 32;
        } else if (cp >= 0x410 && cp <= 0x42F) {
            lower = cp + 32;
        }
        appendUtf8(out, lower);
    }
    return out;
}

std::string trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end) {
        std::size_t i = begin;
        const uint32_t cp = decodeUtf8(text, i);
        if (cp == 0xFFFFFFFFu || !isJsSpace(cp)) break;
        begin = i;
    }

    while (end > begin) {
        /* step back to the start of the last code point */
        std::size_t start = end - 1;
        while (start > begin && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
            --start;
        }
        std::size_t i = start;
        const uint32_t cp = decodeUtf8(text, i);
        if (cp == 0xFFFFFFFFu || i != end || !isJsSpace(cp)) break;
        end = start;
    }

    return std::string(text.substr(begin, end - begin));
}

bool includes(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string_view::npos;
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

std::string toISOString(double epochMs)
{
    if (!std::isfinite(epochMs)) throw std::range_error("Invalid time value");

    const auto ms = static_cast<int64_t>(epochMs);
    int64_t days = ms / 86400000;
    int64_t rem = ms % 86400000;
    if (rem < 0) {
        rem += 86400000;
        days -= 1;
    }

    int64_t y = 0;
    unsigned m = 0, d = 0;
    civilFromDays(days, y, m, d);

    const auto hh = static_cast<int>(rem / 3600000);
    const auto mm = static_cast<int>((rem / 60000) % 60);
    const auto ss = static_cast<int>((rem / 1000) % 60);
    const auto mss = static_cast<int>(rem % 1000);

    char buf[64];
    if (y >= 0 && y <= 9999) {
        std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02d:%02d:%02d.%03dZ",
                      static_cast<long long>(y), m, d, hh, mm, ss, mss);
    } else {
        std::snprintf(buf, sizeof buf, "%c%06lld-%02u-%02uT%02d:%02d:%02d.%03dZ",
                      y < 0 ? '-' : '+', static_cast<long long>(y < 0 ? -y : y), m, d,
                      hh, mm, ss, mss);
    }
    return buf;
}

double parseDate(std::string_view raw)
{
    const std::string text = trim(raw);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::size_t i = 0;

    auto digits = [&](int count, int& out) -> bool {
        if (i + static_cast<std::size_t>(count) > text.size()) return false;
        int v = 0;
        for (int k = 0; k < count; ++k) {
            const char c = text[i + static_cast<std::size_t>(k)];
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        i += static_cast<std::size_t>(count);
        out = v;
        return true;
    };

    int year = 0, month = 1, day = 1, hour = 0, minute = 0, second = 0, millis = 0;
    if (!digits(4, year)) return nan;

    bool hasTime = false;
    bool hasOffset = false;
    int offsetMinutes = 0;

    if (i < text.size() && text[i] == '-') {
        ++i;
        if (!digits(2, month)) return nan;
        if (i < text.size() && text[i] == '-') {
            ++i;
            if (!digits(2, day)) return nan;
        }
    }

    if (i < text.size() && (text[i] == 'T' || text[i] == 't' || text[i] == ' ')) {
        ++i;
        hasTime = true;
        if (!digits(2, hour)) return nan;
        if (i >= text.size() || text[i] != ':') return nan;
        ++i;
        if (!digits(2, minute)) return nan;
        if (i < text.size() && text[i] == ':') {
            ++i;
            if (!digits(2, second)) return nan;
            if (i < text.size() && text[i] == '.') {
                ++i;
                int frac = 0, count = 0;
                while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                    if (count < 3) frac = frac * 10 + (text[i] - '0');
                    ++count;
                    ++i;
                }
                if (count == 0) return nan;
                for (int k = count; k < 3; ++k) frac *= 10;
                millis = frac;
            }
        }
        if (i < text.size() && (text[i] == 'Z' || text[i] == 'z')) {
            ++i;
            hasOffset = true;
        } else if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            const int sign = text[i] == '-' ? -1 : 1;
            ++i;
            int oh = 0, om = 0;
            if (!digits(2, oh)) return nan;
            if (i < text.size() && text[i] == ':') ++i;
            if (!digits(2, om)) return nan;
            hasOffset = true;
            offsetMinutes = sign * (oh * 60 + om);
        }
    }

    if (i != text.size()) return nan;
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 24 || minute > 59 ||
        second > 59) {
        return nan;
    }
    if (hour == 24 && (minute != 0 || second != 0 || millis != 0)) return nan;

    const int64_t days = daysFromCivil(year, static_cast<unsigned>(month),
                                       static_cast<unsigned>(day));
    double ms = static_cast<double>(days) * 86400000.0 + hour * 3600000.0 +
                minute * 60000.0 + second * 1000.0 + millis;

    if (hasTime) {
        if (hasOffset) {
            ms -= offsetMinutes * 60000.0;
        } else {
            ms -= 330 * 60000.0;  // site time, IST
        }
    }
    return ms;
}

std::string stringify(const Json& value, int indent)
{
    std::string out;
    write(out, value, indent, 0);
    return out;
}

Json parse(std::string_view text)
{
    try {
        return Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& error) {
        std::string message = SyntaxCheck(text).run();
        if (message.empty()) {
            message = error.what();
            const auto colon = message.rfind(": ");
            if (colon != std::string::npos) message = message.substr(colon + 2);
        }
        throw std::runtime_error(message);
    }
}

double toNumber(const Json& value)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    switch (value.type()) {
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned:
    case Json::value_t::number_float:
        return value.get<double>();
    case Json::value_t::boolean:
        return value.get<bool>() ? 1 : 0;
    case Json::value_t::null:
        return 0;
    case Json::value_t::string: {
        const std::string text = trim(value.get_ref<const std::string&>());
        if (text.empty()) return 0;
        if (text == "Infinity" || text == "+Infinity") return std::numeric_limits<double>::infinity();
        if (text == "-Infinity") return -std::numeric_limits<double>::infinity();
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            double v = 0;
            for (std::size_t k = 2; k < text.size(); ++k) {
                const char c = text[k];
                int d = -1;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                if (d < 0) return nan;
                v = v * 16 + d;
            }
            return v;
        }
        /* StrDecimalLiteral: sign, digits, optional fraction, exponent. */
        std::size_t k = 0;
        if (text[k] == '+' || text[k] == '-') ++k;
        bool any = false;
        while (k < text.size() && text[k] >= '0' && text[k] <= '9') { ++k; any = true; }
        if (k < text.size() && text[k] == '.') {
            ++k;
            while (k < text.size() && text[k] >= '0' && text[k] <= '9') { ++k; any = true; }
        }
        if (!any) return nan;
        if (k < text.size() && (text[k] == 'e' || text[k] == 'E')) {
            ++k;
            if (k < text.size() && (text[k] == '+' || text[k] == '-')) ++k;
            bool exp = false;
            while (k < text.size() && text[k] >= '0' && text[k] <= '9') { ++k; exp = true; }
            if (!exp) return nan;
        }
        if (k != text.size()) return nan;
        return parseFloat(text);
    }
    default:
        return nan;
    }
}

}  // namespace dhun::js
