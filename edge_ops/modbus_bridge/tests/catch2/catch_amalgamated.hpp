// ============================================================================
// Catch2 v3 Amalgamated Header — STUB
// ============================================================================
//
// This is a STUB placeholder. The real Catch2 v3.5+ amalgamated header must be
// downloaded from https://github.com/catchorg/Catch2/releases and placed here
// before building the test suite.
//
// Download instructions:
//   1. Go to https://github.com/catchorg/Catch2/releases
//   2. Download the amalgamated header from the assets of the latest v3.x release
//   3. Replace this file with the real catch_amalgamated.hpp
//   4. Replace catch_amalgamated.cpp with the real catch_amalgamated.cpp
//
// This stub provides the minimal macro definitions needed for the test file
// to be syntactically valid, but will NOT produce a working test runner.
//
// ============================================================================

#ifndef CATCH_AMALGAMATED_HPP_INCLUDED
#define CATCH_AMALGAMATED_HPP_INCLUDED

// ============================================================================
// Minimal Catch2 v3 stub macros — for compilation only, NOT for execution
// Replace this entire file with the real amalgamated header before building.
// ============================================================================

#include <string>
#include <sstream>
#include <vector>
#include <cmath>
#include <iostream>

namespace Catch {

// Approx class for floating-point tolerance comparisons
class Approx {
public:
    explicit Approx(double value) : m_value(value), m_epsilon(static_cast<double>(1e-4)), m_scale(0.0) {}

    Approx& epsilon(double newEpsilon) { m_epsilon = newEpsilon; return *this; }
    Approx& scale(double newScale) { m_scale = newScale; return *this; }

    friend bool operator==(double lhs, const Approx& rhs) {
        return std::fabs(lhs - rhs.m_value) < rhs.m_epsilon * (rhs.m_scale + std::fabs(rhs.m_value));
    }
    friend bool operator==(const Approx& lhs, double rhs) {
        return operator==(rhs, lhs);
    }

    friend std::ostream& operator<<(std::ostream& os, const Approx& a) {
        os << "Approx( " << a.m_value << " )";
        return os;
    }

private:
    double m_value;
    double m_epsilon;
    double m_scale;
};

// Test registration infrastructure (stub)
struct TestCaseInfo {
    std::string name;
    std::string tags;
};

struct SourceLineInfo {
    std::string file;
    unsigned int line;
};

class ITestCase {
public:
    virtual ~ITestCase() = default;
    virtual void invoke() const = 0;
};

class TestRegistry {
public:
    static TestRegistry& instance() {
        static TestRegistry reg;
        return reg;
    }

    void registerTest(const TestCaseInfo& info, ITestCase* test) {
        m_tests.push_back({info, test});
    }

    void runAll() const;

private:
    TestRegistry() = default;
    struct TestEntry {
        TestCaseInfo info;
        ITestCase* test;
    };
    std::vector<TestEntry> m_tests;
};

// Auto-registration helper
struct TestRegistrar {
    TestRegistrar(const std::string& name, const std::string& tags, ITestCase* test);
};

// Assertion handlers (stub)
void handleAssert(bool result, const char* expr, const char* file, unsigned int line);

} // namespace Catch

// ============================================================================
// Catch2 v3 macro definitions — STUB
// ============================================================================

#define TEST_CASE(name, ...) \
    static void CATCH2_INTERNAL_TEST_##name(); \
    namespace { \
        struct CATCH2_INTERNAL_TestClass_##name : public Catch::ITestCase { \
            void invoke() const override { CATCH2_INTERNAL_TEST_##name(); } \
        }; \
        static CATCH2_INTERNAL_TestClass_##name CATCH2_INTERNAL_TestInstance_##name; \
        static Catch::TestRegistrar CATCH2_INTERNAL_Reg_##name(#name, "", &CATCH2_INTERNAL_TestInstance_##name); \
    } \
    static void CATCH2_INTERNAL_TEST_##name()

#define SECTION(name) \
    for (int CATCH2_INTERNAL_SECTION_##name = 1; CATCH2_INTERNAL_SECTION_##name; CATCH2_INTERNAL_SECTION_##name = 0)

#define REQUIRE(expr) \
    Catch::handleAssert(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define REQUIRE_THAT(expr, matcher) \
    Catch::handleAssert(static_cast<bool>(matcher), #expr " " #matcher, __FILE__, __LINE__)

#define CHECK(expr) \
    Catch::handleAssert(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define FAIL(msg) \
    do { std::cerr << "FAIL: " << msg << std::endl; std::abort(); } while(0)

#endif // CATCH_AMALGAMATED_HPP_INCLUDED