// Copyright Epic Games, Inc. All Rights Reserved.

#include "trimdtests/Defs.h"

#include "trimd/TRiMD.h"

#ifdef TRIMD_ENABLE_SSE
using T128Types = ::testing::Types<trimd::scalar::F128, trimd::sse::F128>;
#else
using T128Types = ::testing::Types<trimd::scalar::F128>;
#endif  // TRIMD_ENABLE_SSE

template<typename T>
class T128Test : public ::testing::Test {
protected:
    using T128 = T;

};

TYPED_TEST_SUITE(T128Test, T128Types, );

TYPED_TEST(T128Test, CheckSize) {
    ASSERT_EQ(TestFixture::T128::size(), 4ul);
}

TYPED_TEST(T128Test, Equality) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{1.0f, 2.0f, 3.0f, 4.0f};

    typename TestFixture::T128 v3{1.5f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v4{1.0f, 2.5f, 3.0f, 4.0f};
    typename TestFixture::T128 v5{1.0f, 2.0f, 3.5f, 4.0f};
    typename TestFixture::T128 v6{1.0f, 2.0f, 3.0f, 4.5f};

    ASSERT_TRUE(v1 == v2);
    ASSERT_FALSE(v1 == v3);
    ASSERT_FALSE(v1 == v4);
    ASSERT_FALSE(v1 == v5);
    ASSERT_FALSE(v1 == v6);
}

TYPED_TEST(T128Test, Inequality) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{1.0f, 2.0f, 3.0f, 4.0f};
    ASSERT_FALSE(v1 != v2);
}

TYPED_TEST(T128Test, ConstructFromArgs) {
    typename TestFixture::T128 v{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 expected{1.0f, 2.0f, 3.0f, 4.0f};
    ASSERT_EQ(v, expected);
}

TYPED_TEST(T128Test, ConstructFromSingleValue) {
    typename TestFixture::T128 v{42.0f};
    typename TestFixture::T128 expected{42.0f, 42.0f, 42.0f, 42.0f};
    ASSERT_EQ(v, expected);
}

TYPED_TEST(T128Test, FromAlignedSource) {
    alignas(TestFixture::T128::alignment()) const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f};
    auto v = TestFixture::T128::fromAlignedSource(expected);

    alignas(TestFixture::T128::alignment()) float result[TestFixture::T128::size()];
    v.alignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T128::size());
}

TYPED_TEST(T128Test, AlignedLoadStore) {
    alignas(TestFixture::T128::alignment()) const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v;
    v.alignedLoad(expected);

    alignas(TestFixture::T128::alignment()) float result[TestFixture::T128::size()];
    v.alignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T128::size());
}

TYPED_TEST(T128Test, FromUnalignedSource) {
    const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f};
    auto v = TestFixture::T128::fromUnalignedSource(expected);

    float result[TestFixture::T128::size()];
    v.unalignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T128::size());
}

TYPED_TEST(T128Test, UnalignedLoadStore) {
    const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v;
    v.unalignedLoad(expected);

    float result[TestFixture::T128::size()];
    v.unalignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T128::size());
}

TYPED_TEST(T128Test, LoadSingleValue) {
    const float source[] = {42.0f, 43.0f, 44.0f, 45.0f};
    auto v = TestFixture::T128::loadSingleValue(source);
    typename TestFixture::T128 expected{42.0f, 0.0f, 0.0f, 0.0f};
    ASSERT_EQ(v, expected);
}

TYPED_TEST(T128Test, Sum) {
    typename TestFixture::T128 v{1.0f, 2.0f, 3.0f, 4.0f};
    ASSERT_EQ(v.sum(), 10.0f);
}

TYPED_TEST(T128Test, CompoundAssignmentAdd) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{3.0f, 4.0f, 5.0f, 6.0f};
    typename TestFixture::T128 expected{4.0f, 6.0f, 8.0f, 10.0f};
    v1 += v2;
    ASSERT_EQ(v1, expected);
}

TYPED_TEST(T128Test, CompoundAssignmentSub) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{3.0f, 4.0f, 5.0f, 6.0f};
    typename TestFixture::T128 expected{-2.0f, -2.0f, -2.0f, -2.0f};
    v1 -= v2;
    ASSERT_EQ(v1, expected);
}

TYPED_TEST(T128Test, CompoundAssignmentMul) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{3.0f, 4.0f, 5.0f, 6.0f};
    typename TestFixture::T128 expected{3.0f, 8.0f, 15.0f, 24.0f};
    v1 *= v2;
    ASSERT_EQ(v1, expected);
}

TYPED_TEST(T128Test, CompoundAssignmentDiv) {
    typename TestFixture::T128 v1{4.0f, 3.0f, 9.0f, 12.0f};
    typename TestFixture::T128 v2{1.0f, 2.0f, 3.0f, 3.0f};
    float expected[TestFixture::T128::size()] = {4.0f, 1.5f, 3.0f, 4.0f};
    v1 /= v2;

    float result[TestFixture::T128::size()];
    v1.unalignedStore(result);

    ASSERT_ELEMENTS_NEAR(result, expected, TestFixture::T128::size(), 0.0001f);
}

TYPED_TEST(T128Test, OperatorAdd) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{3.0f, 4.0f, 5.0f, 6.0f};
    typename TestFixture::T128 expected{4.0f, 6.0f, 8.0f, 10.0f};
    auto v3 = v1 + v2;
    ASSERT_EQ(v3, expected);
}

TYPED_TEST(T128Test, OperatorSub) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{3.0f, 4.0f, 5.0f, 6.0f};
    typename TestFixture::T128 expected{-2.0f, -2.0f, -2.0f, -2.0f};
    auto v3 = v1 - v2;
    ASSERT_EQ(v3, expected);
}

TYPED_TEST(T128Test, OperatorMul) {
    typename TestFixture::T128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    typename TestFixture::T128 v2{3.0f, 4.0f, 5.0f, 6.0f};
    typename TestFixture::T128 expected{3.0f, 8.0f, 15.0f, 24.0f};
    auto v3 = v1 * v2;
    ASSERT_EQ(v3, expected);
}

TYPED_TEST(T128Test, OperatorDiv) {
    typename TestFixture::T128 v1{4.0f, 3.0f, 9.0f, 12.0f};
    typename TestFixture::T128 v2{1.0f, 2.0f, 3.0f, 3.0f};
    float expected[TestFixture::T128::size()] = {4.0f, 1.5f, 3.0f, 4.0f};
    auto v3 = v1 / v2;

    float result[TestFixture::T128::size()];
    v3.unalignedStore(result);

    ASSERT_ELEMENTS_NEAR(result, expected, TestFixture::T128::size(), 0.0001f);
}

TEST(T128Test, TransposeSquareScalar) {
    trimd::scalar::F128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    trimd::scalar::F128 v2{1.0f, 2.0f, 3.0f, 4.0f};
    trimd::scalar::F128 v3{1.0f, 2.0f, 3.0f, 4.0f};
    trimd::scalar::F128 v4{1.0f, 2.0f, 3.0f, 4.0f};

    trimd::scalar::transpose(v1, v2, v3, v4);

    trimd::scalar::F128 e1{1.0f, 1.0f, 1.0f, 1.0f};
    trimd::scalar::F128 e2{2.0f, 2.0f, 2.0f, 2.0f};
    trimd::scalar::F128 e3{3.0f, 3.0f, 3.0f, 3.0f};
    trimd::scalar::F128 e4{4.0f, 4.0f, 4.0f, 4.0f};

    ASSERT_EQ(v1, e1);
    ASSERT_EQ(v2, e2);
    ASSERT_EQ(v3, e3);
    ASSERT_EQ(v4, e4);
}

#ifdef TRIMD_ENABLE_SSE
TEST(T128Test, TransposeSquareSSE) {
    trimd::sse::F128 v1{1.0f, 2.0f, 3.0f, 4.0f};
    trimd::sse::F128 v2{1.0f, 2.0f, 3.0f, 4.0f};
    trimd::sse::F128 v3{1.0f, 2.0f, 3.0f, 4.0f};
    trimd::sse::F128 v4{1.0f, 2.0f, 3.0f, 4.0f};

    trimd::sse::transpose(v1, v2, v3, v4);

    trimd::sse::F128 e1{1.0f, 1.0f, 1.0f, 1.0f};
    trimd::sse::F128 e2{2.0f, 2.0f, 2.0f, 2.0f};
    trimd::sse::F128 e3{3.0f, 3.0f, 3.0f, 3.0f};
    trimd::sse::F128 e4{4.0f, 4.0f, 4.0f, 4.0f};

    ASSERT_EQ(v1, e1);
    ASSERT_EQ(v2, e2);
    ASSERT_EQ(v3, e3);
    ASSERT_EQ(v4, e4);
}
#endif  // TRIMD_ENABLE_SSE
