/* ============================================================
   UNIT TESTS
   Minimal runner (no framework dependency). Suites live in
   unit_*.inc and register themselves through TEST().
   ============================================================ */

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "dhun/js.h"

namespace dhun::test {

struct Case {
    std::string name;
    std::function<void()> body;
};

std::vector<Case>& registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> body)
    {
        registry().push_back({std::move(name), std::move(body)});
    }
};

int g_failures = 0;
std::string g_current;

void fail(const std::string& what, const char* file, int line)
{
    ++g_failures;
    std::cerr << "  FAIL [" << g_current << "] " << file << ":" << line << "  " << what << "\n";
}

}  // namespace dhun::test

#define DHUN_CAT2(a, b) a##b
#define DHUN_CAT(a, b) DHUN_CAT2(a, b)
#define TEST(name)                                                                  \
    static void DHUN_CAT(test_fn_, __LINE__)();                                     \
    static dhun::test::Registrar DHUN_CAT(test_reg_, __LINE__)(name, DHUN_CAT(test_fn_, __LINE__)); \
    static void DHUN_CAT(test_fn_, __LINE__)()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) dhun::test::fail(#cond, __FILE__, __LINE__);                    \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        const auto& _a = (a);                                                       \
        const auto& _b = (b);                                                       \
        if (!(_a == _b)) {                                                          \
            dhun::test::fail(std::string(#a " == " #b), __FILE__, __LINE__);       \
        }                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                       \
    do {                                                                            \
        const double _a = (a);                                                      \
        const double _b = (b);                                                      \
        if (!(std::fabs(_a - _b) <= (tol))) {                                       \
            dhun::test::fail(std::string(#a " ~= " #b " (") + dhun::js::toString(_a) + \
                                 " vs " + dhun::js::toString(_b) + ")",            \
                             __FILE__, __LINE__);                                   \
        }                                                                           \
    } while (0)

#include "unit_foundation.inc"

int main()
{
    int ran = 0;
    for (const auto& c : dhun::test::registry()) {
        dhun::test::g_current = c.name;
        const int before = dhun::test::g_failures;
        try {
            c.body();
        } catch (const std::exception& e) {
            dhun::test::fail(std::string("threw: ") + e.what(), __FILE__, __LINE__);
        }
        ++ran;
        std::cout << (dhun::test::g_failures == before ? "[ ok ] " : "[FAIL] ") << c.name << "\n";
    }
    std::cout << ran << " tests, " << dhun::test::g_failures << " failed checks\n";
    return dhun::test::g_failures == 0 ? 0 : 1;
}
