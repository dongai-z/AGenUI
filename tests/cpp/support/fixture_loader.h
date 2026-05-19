// Helpers for loading test fixture files.
//
// Tests reference files relative to TESTS_FIXTURE_DIR (a compile-time macro
// set by tests/cpp/CMakeLists.txt).

#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef TESTS_FIXTURE_DIR
#error "TESTS_FIXTURE_DIR must be defined by the build system."
#endif

namespace agenui {
namespace testing {

inline std::string FixturePath(const std::string& relative) {
    return std::string(TESTS_FIXTURE_DIR) + "/" + relative;
}

inline std::string LoadFixtureOrEmpty(const std::string& relative) {
    std::ifstream file(FixturePath(relative));
    if (!file.is_open()) return {};
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline std::string LoadFixtureOrThrow(const std::string& relative) {
    std::ifstream file(FixturePath(relative));
    if (!file.is_open()) {
        throw std::runtime_error("Fixture not found: " + FixturePath(relative));
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

}  // namespace testing
}  // namespace agenui
