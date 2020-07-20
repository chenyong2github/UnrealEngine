#pragma once

#ifdef _MSC_VER
    #pragma warning(disable:4668)
    #pragma warning(disable:4996)
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
#include <gtest/gtest.h>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
#ifdef _MSC_VER
    #pragma warning(pop)
    #pragma warning(default:4668)
    #pragma warning(default:4996)
#endif

#ifndef INSTANTIATE_TEST_SUITE_P
    #define INSTANTIATE_TEST_SUITE_P INSTANTIATE_TEST_CASE_P
#endif

#ifndef TYPED_TEST_SUITE_P
    #define TYPED_TEST_SUITE_P TYPED_TEST_CASE_P
#endif

#ifndef REGISTER_TYPED_TEST_SUITE_P
    #define REGISTER_TYPED_TEST_SUITE_P REGISTER_TYPED_TEST_CASE_P
#endif

#ifndef INSTANTIATE_TYPED_TEST_SUITE_P
    #define INSTANTIATE_TYPED_TEST_SUITE_P INSTANTIATE_TYPED_TEST_CASE_P
#endif

#define ASSERT_ELEMENTS_EQ(result, expected, count)   \
    for (std::size_t i = 0ul; i < count; ++i) {         \
        ASSERT_EQ(result[i], expected[i]);              \
    }

#define ASSERT_ELEMENTS_NEAR(lhs, rhs, threshold)       \
    ASSERT_EQ(lhs.size(), rhs.size());                  \
    for (std::size_t i = 0ul; i < lhs.size(); ++i) {    \
        ASSERT_NEAR(lhs[i], rhs[i], threshold);         \
    }
