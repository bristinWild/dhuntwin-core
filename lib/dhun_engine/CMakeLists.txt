# dhun_engine — the DhunTwin OS engine as a plain C++20 library.
# No Qt, no Logos: the dhuntwin_core module wraps it, and anything
# else (a CLI, a test, another UI) can link it directly.

cmake_minimum_required(VERSION 3.16)
project(dhun_engine LANGUAGES CXX)

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json 3.10 REQUIRED)
endif()

# ---- embedded reference data -------------------------------------
# data/*.json is extracted from the upstream TypeScript app by
# scripts/extract-data.ts and compiled into the library so the
# module ships as one file.
set(DHUN_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(GLOB DHUN_DATA_FILES CONFIGURE_DEPENDS ${DHUN_DATA_DIR}/*.json)
set(DHUN_EMBED_CPP ${CMAKE_CURRENT_BINARY_DIR}/dhun_embedded_data.cpp)

add_custom_command(
    OUTPUT ${DHUN_EMBED_CPP}
    COMMAND ${CMAKE_COMMAND}
        -DDATA_DIR=${DHUN_DATA_DIR}
        -DOUTPUT=${DHUN_EMBED_CPP}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedData.cmake
    DEPENDS ${DHUN_DATA_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedData.cmake
    COMMENT "Embedding DhunTwin reference data"
    VERBATIM
)

add_library(dhun_engine STATIC
    src/js.cpp
    src/phases.cpp
    src/clock.cpp
    src/format.cpp
    src/catalog.cpp
    src/simulation.cpp
    src/planning.cpp
    src/commons.cpp
    ${DHUN_EMBED_CPP}
)

target_include_directories(dhun_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(dhun_engine PUBLIC cxx_std_20)
target_link_libraries(dhun_engine PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(dhun_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dhun_engine PRIVATE -Wall -Wextra)
endif()

# ---- tests ---------------------------------------------------------
option(DHUN_ENGINE_BUILD_TESTS "Build the dhun_engine unit and parity tests" OFF)

if(DHUN_ENGINE_BUILD_TESTS)
    enable_testing()

    get_filename_component(DHUN_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)

    add_executable(dhun_parity_test
        tests/parity_test.cpp
        tests/parity_cases.cpp
    )
    target_link_libraries(dhun_parity_test PRIVATE dhun_engine)
    target_compile_definitions(dhun_parity_test PRIVATE
        DHUN_GOLDEN_DIR="${DHUN_REPO_ROOT}/tests/golden")
    add_test(NAME parity COMMAND dhun_parity_test)

    add_executable(dhun_unit_test tests/unit_test.cpp)
    target_link_libraries(dhun_unit_test PRIVATE dhun_engine)
    target_compile_definitions(dhun_unit_test PRIVATE
        DHUN_DATA_DIR="${DHUN_DATA_DIR}")
    add_test(NAME unit COMMAND dhun_unit_test)
endif()
