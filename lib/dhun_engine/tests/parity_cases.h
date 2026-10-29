#pragma once

#include <functional>
#include <map>
#include <string>

#include "compare.h"

namespace dhun::test {

using ParityCase = std::function<void(const Json& golden, Report& report)>;

/* name -> runner; the name is the golden file stem. */
const std::map<std::string, ParityCase>& parityCases();

}  // namespace dhun::test
