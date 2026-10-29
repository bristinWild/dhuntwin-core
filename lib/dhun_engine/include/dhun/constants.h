#pragma once

/* ============================================================
   DHUN TWIN OS — CONSTANTS
   Port of frontend/src/features/dhun/data/constants.ts.
   Every value is cross-checked against data/constants.json (the
   upstream literals) by the unit tests.
   ============================================================ */

namespace dhun {

/* Masterplan boundary constraints */
inline constexpr double TOTAL_SITE_ACRES = 500;
inline constexpr double BIORESERVE_ACRES = 400;
inline constexpr double MAX_CONSTRUCTION_ACRES = 100;
inline constexpr double TREE_HEIGHT_LIMIT_M = 12;
inline constexpr double AFFORESTATION_OFFSET_ACRES = 124;
inline constexpr double ANNUAL_WATER_CAPACITY_KL = 550000;

/* Emission factors — Dhun KPI report (Rev 2, July 2022) & CEA */
inline constexpr double GRID_FACTOR = 0.85;
inline constexpr double SOLAR_FACTOR = 0.05;
inline constexpr double PETROL_FACTOR = 2.296;
inline constexpr double DIESEL_FACTOR = 2.653;
inline constexpr double LPG_FACTOR = 2.983;

/* Solar resource — KPI report, Jaipur proxy */
inline constexpr double SPECIFIC_YIELD = 1547;
inline constexpr double SOLAR_PEAK_FACTOR = 0.56;
inline constexpr double SOILING_FACTOR = 0.97;

/* Demand — KPI report + EcoNiwas reference case */
inline constexpr double EPI_TARGET = 35;
inline constexpr double EPI_EFFICIENT = 47.1;
inline constexpr double EPI_BASELINE = 134;

/* Water — KPI report */
inline constexpr double LITRES_PER_PERSON_DAY = 78;
inline constexpr double LANDSCAPE_LITRES_M2_DAY = 1.16;
inline constexpr double STP_ENERGY_KWH_PER_KL = 0.5;
inline constexpr double PUMP_ENERGY_KWH_PER_KL = 0.4;

/* Population — KPI report design population */
inline constexpr double DESIGN_POPULATION = 11847;

/* Carbon targets — KPI report */
inline constexpr double TARGET_PER_CAPITA_T = 0.4;
inline constexpr double UPFRONT_KG_M2 = 225;
inline constexpr double USE_EMBODIED_KG_M2 = 287.5;
inline constexpr double BUILD_OUT_M2 = 650558;

/* Living carbon */
inline constexpr double STANDING_STOCK_T = 45000;
inline constexpr double BIO_SEQUESTRATION_T_YR = 1500;
inline constexpr double FARM_SEQUESTRATION_T_YR = 300;
inline constexpr double TOTAL_SEQUESTRATION_T_YR =
    BIO_SEQUESTRATION_T_YR + FARM_SEQUESTRATION_T_YR;

inline constexpr double SELF_CONSUMPTION_FACTOR = 0.973;

/* Energy economics — planning assumptions */
inline constexpr double PV_CAPEX_INR_PER_KWP = 35000;
inline constexpr double BATTERY_CAPEX_INR_PER_KWH = 18000;
inline constexpr double BATTERY_ROUND_TRIP_EFFICIENCY = 0.88;
inline constexpr double SYSTEM_LIFETIME_YR = 25;
inline constexpr double OANDM_FRACTION = 0.015;
inline constexpr double TARIFF_INR_PER_KWH = 8;

/* Landscape water */
inline constexpr double GREEN_LAND_M2 = 308000;
inline constexpr double LANDSCAPE_KL_YR_BUILD_OUT = 240317;

}  // namespace dhun
