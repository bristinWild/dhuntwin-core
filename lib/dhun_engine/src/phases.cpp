#include "dhun/phases.h"

namespace dhun {

namespace {

const PhaseConfig PHASES[3] = {
    {
        PhaseId::Today,
        "today",
        "Today · 2026",
        "2026",
        "Field station operating. School under construction, no solar farm yet, compute is a single workstation, spirulina pilot raceway active.",
        140,
        6000,
        55,
        0,
        1.2,
        1.6,
        1.5,
        12,
        0.05,
        12000,
        35,
        50000,
    },
    {
        PhaseId::Phase1,
        "phase1",
        "Phase 1 · 2029",
        "2029",
        "R1 residences and the 7-acre school complete. 420 kWp rooftop solar, shared 8-GPU compute node, expanded spirulina cultivation.",
        900,
        72000,
        47.1,
        420,
        10,
        1.45,
        4.6,
        70,
        0.12,
        40000,
        70,
        140000,
    },
    {
        PhaseId::Buildout,
        "buildout",
        "Build-out · 2035",
        "2035",
        "Full masterplan: 11,847 residents, 650,558 m² built, 16,816 kWp solar, flagship compute commons, commercial biomass facility.",
        11847,
        650558,
        47.1,
        16816,
        28,
        1.45,
        15.5,
        832,
        1.52,
        306760,
        124,
        300000,
    },
};

}  // namespace

const PhaseConfig& phaseConfig(PhaseId id)
{
    return PHASES[static_cast<int>(id)];
}

std::string_view phaseKey(PhaseId id)
{
    return phaseConfig(id).key;
}

std::optional<PhaseId> parsePhase(std::string_view key)
{
    for (const auto& config : PHASES) {
        if (key == config.key) return config.id;
    }
    return std::nullopt;
}

int phaseStartYear(PhaseId id)
{
    switch (id) {
    case PhaseId::Today: return 2026;
    case PhaseId::Phase1: return 2029;
    case PhaseId::Buildout: return 2035;
    }
    return 2026;
}

Json phaseToJson(const PhaseConfig& c)
{
    Json j;
    j["id"] = c.key;
    j["label"] = c.label;
    j["short"] = c.shortLabel;
    j["description"] = c.description;
    j["population"] = c.population;
    j["builtM2"] = c.builtM2;
    j["epi"] = c.epi;
    j["solarKwp"] = c.solarKwp;
    j["computeKw"] = c.computeKw;
    j["computePue"] = c.computePue;
    j["spirulinaKw"] = c.spirulinaKw;
    j["stpKlDay"] = c.stpKlDay;
    j["organicTDay"] = c.organicTDay;
    j["harvestKlYr"] = c.harvestKlYr;
    j["forestAcres"] = c.forestAcres;
    j["treeCount"] = c.treeCount;
    return j;
}

}  // namespace dhun
