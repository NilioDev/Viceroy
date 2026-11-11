// SPDX-License-Identifier: GPL-3.0-or-later
//
// A deliberately tiny test harness, so the core can be tested without
// third-party dependencies.
//
//   VCR_TEST(name) { VCR_CHECK(1 + 1 == 2); }
#pragma once

#include <functional>
#include <string_view>
#include <vector>

namespace viceroy::test {

struct Case {
    std::string_view name;
    std::function<void()> body;
};

std::vector<Case>& registry();
void reportFailure(const char* file, int line, const char* expression);
void reportSkip(const char* reason);

struct Registrar {
    Registrar(std::string_view name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

} // namespace viceroy::test

#define VCR_TEST_CONCAT_INNER(a, b) a##b
#define VCR_TEST_CONCAT(a, b) VCR_TEST_CONCAT_INNER(a, b)

#define VCR_TEST(name)                                                                     \
    static void VCR_TEST_CONCAT(vcr_test_fn_, __LINE__)();                                 \
    static const ::viceroy::test::Registrar VCR_TEST_CONCAT(vcr_test_reg_, __LINE__){          \
        name, &VCR_TEST_CONCAT(vcr_test_fn_, __LINE__)};                                   \
    static void VCR_TEST_CONCAT(vcr_test_fn_, __LINE__)()

#define VCR_CHECK(...)                                                              \
    do {                                                                                   \
        if (!(__VA_ARGS__)) {                                                               \
            ::viceroy::test::reportFailure(__FILE__, __LINE__, #__VA_ARGS__);                   \
        }                                                                                  \
    } while (false)

// Ends the current test early and reports it as skipped, for tests that need
// files which are not always present (such as the original game data).
#define VCR_SKIP(reason)                                                                   \
    do {                                                                                   \
        ::viceroy::test::reportSkip(reason);                                                   \
        return;                                                                            \
    } while (false)
