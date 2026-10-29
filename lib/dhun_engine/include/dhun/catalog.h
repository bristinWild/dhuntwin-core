#pragma once

/* ============================================================
   REFERENCE CATALOG
   The upstream app's static catalogues (asset templates, seed
   assets, GIS layers, utility bills, parameter register, ...),
   extracted to the data/ JSON files by scripts/extract-data.ts and compiled
   into the library. Key order is the upstream literal order.
   ============================================================ */

#include <string>
#include <string_view>
#include <vector>

#include "dhun/js.h"

namespace dhun::catalog {

/* Parsed dataset by file stem ("seed_assets", "gis", ...). Parsed
   once on first use; throws std::out_of_range for unknown names. */
const Json& get(std::string_view name);

/* Raw JSON text of a dataset, exactly as shipped. */
std::string_view raw(std::string_view name);

std::vector<std::string> names();

}  // namespace dhun::catalog
