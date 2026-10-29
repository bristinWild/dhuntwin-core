# dhuntwin_core — DhunTwin OS engine as a Logos core module.
# Universal authoring model: we write only src/dhuntwin_core_impl.{h,cpp};
# logos-module-builder generates the LIDL contract, the Qt plugin glue and
# the Qt-free C ABI from the impl header.

cmake_minimum_required(VERSION 3.16)
project(DhuntwinCorePlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found")
endif()

# Module name from metadata.json — single source of truth.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

# The engine: plain C++20, no Qt, no Logos. Unit and parity tests build
# separately (scripts/test-engine.sh); the module only links the library.
set(DHUN_ENGINE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/dhun_engine)

logos_module(
    NAME ${MODULE_NAME}
    SOURCES
        src/dhuntwin_core_impl.h
        src/dhuntwin_core_impl.cpp
    LINK_TARGETS
        dhun_engine
)
