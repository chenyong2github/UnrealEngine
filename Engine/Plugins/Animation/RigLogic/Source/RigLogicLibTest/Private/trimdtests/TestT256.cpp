// Copyright Epic Games, Inc. All Rights Reserved.

#include "trimdtests/Defs.h"

#include "trimd/TRiMD.h"

#ifdef TRIMD_ENABLE_AVX
using T256Types = ::testing::Types<trimd::scalar::F256, trimd::avx::F256>;
#else
using T256Types = ::testing::Types<trimd::scalar::F256>;
#endif  // TRIMD_ENABLE_SSE

template<typename T>
class T256Test : public ::testing::Test {
protected:
    using T256 = T;

};

TYPED_TEST_SUITE(T256Test, T256Types, );

TYPED_TEST(T256Test, CheckSize) {
    ASSERT_EQ(TestFixture::T256::size(), 8ul);
}

TYPED_TEST(T256Test, Equality) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    typename TestFixture::T256 v3{1.5f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v4{1.0f, 2.5f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v5{1.0f, 2.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v6{1.0f, 2.0f, 3.0f, 4.5f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v7{1.0f, 2.0f, 3.0f, 4.0f, 5.5f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v8{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.5f, 7.0f, 8.0f};
    typename TestFixture::T256 v9{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.5f, 8.0f};
    typename TestFixture::T256 v10{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.5f};

    ASSERT_TRUE(v1 == v2);
    ASSERT_FALSE(v1 == v3);
    ASSERT_FALSE(v1 == v4);
    ASSERT_FALSE(v1 == v5);
    ASSERT_FALSE(v1 == v6);
    ASSERT_FALSE(v1 == v7);
    ASSERT_FALSE(v1 == v8);
    ASSERT_FALSE(v1 == v9);
    ASSERT_FALSE(v1 == v10);
}

TYPED_TEST(T256Test, Inequality) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    ASSERT_FALSE(v1 != v2);
}

TYPED_TEST(T256Test, ConstructFromArgs) {
    typename TestFixture::T256 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 expected{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    ASSERT_EQ(v, expected);
}

TYPED_TEST(T256Test, ConstructFromSingleValue) {
    typename TestFixture::T256 v{42.0f};
    typename TestFixture::T256 expected{42.0f, 42.0f, 42.0f, 42.0f, 42.0f, 42.0f, 42.0f, 42.0f};
    ASSERT_EQ(v, expected);
}

TYPED_TEST(T256Test, FromAlignedSource) {
    alignas(TestFixture::T256::alignment()) const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    auto v = TestFixture::T256::fromAlignedSource(expected);

    alignas(TestFixture::T256::alignment()) float result[TestFixture::T256::size()];
    v.alignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T256::size());
}

TYPED_TEST(T256Test, AlignedLoadStore) {
    alignas(TestFixture::T256::alignment()) const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v;
    v.alignedLoad(expected);

    alignas(TestFixture::T256::alignment()) float result[TestFixture::T256::size()];
    v.alignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T256::size());
}

TYPED_TEST(T256Test, FromUnalignedSource) {
    const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    auto v = TestFixture::T256::fromUnalignedSource(expected);

    float result[TestFixture::T256::size()];
    v.unalignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T256::size());
}

TYPED_TEST(T256Test, UnalignedLoadStore) {
    const float expected[] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v;
    v.unalignedLoad(expected);

    float result[TestFixture::T256::size()];
    v.unalignedStore(result);

    ASSERT_ELEMENTS_EQ(result, expected, TestFixture::T256::size());
}

TYPED_TEST(T256Test, LoadSingleValue) {
    const float source[] = {42.0f, 43.0f, 44.0f, 45.0f, 46.0f, 47.0f, 48.0f, 49.0f};
    auto v = TestFixture::T256::loadSingleValue(source);
    typename TestFixture::T256 expected{42.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    ASSERT_EQ(v, expected);
}

TYPED_TEST(T256Test, Sum) {
    typename TestFixture::T256 v{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    ASSERT_EQ(v.sum(), 36.0f);
}

TYPED_TEST(T256Test, CompoundAssignmentAdd) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    typename TestFixture::T256 expected{4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 18.0f};
    v1 += v2;
    ASSERT_EQ(v1, expected);
}

TYPED_TEST(T256Test, CompoundAssignmentSub) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    typename TestFixture::T256 expected{-2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f};
    v1 -= v2;
    ASSERT_EQ(v1, expected);
}

TYPED_TEST(T256Test, CompoundAssignmentMul) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    typename TestFixture::T256 expected{3.0f, 8.0f, 15.0f, 24.0f, 35.0f, 48.0f, 63.0f, 80.0f};
    v1 *= v2;
    ASSERT_EQ(v1, expected);
}

TYPED_TEST(T256Test, CompoundAssignmentDiv) {
    typename TestFixture::T256 v1{4.0f, 3.0f, 9.0f, 12.0f, 4.0f, 3.0f, 9.0f, 12.0f};
    typename TestFixture::T256 v2{1.0f, 2.0f, 3.0f, 3.0f, 1.0f, 2.0f, 3.0f, 3.0f};
    float expected[TestFixture::T256::size()] = {4.0f, 1.5f, 3.0f, 4.0f, 4.0f, 1.5f, 3.0f, 4.0f};
    v1 /= v2;

    float result[TestFixture::T256::size()];
    v1.unalignedStore(result);

    ASSERT_ELEMENTS_NEAR(result, expected, TestFixture::T256::size(), 0.0001f);
}

TYPED_TEST(T256Test, OperatorAdd) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    typename TestFixture::T256 expected{4.0f, 6.0f, 8.0f, 10.0f, 12.0f, 14.0f, 16.0f, 18.0f};
    auto v3 = v1 + v2;
    ASSERT_EQ(v3, expected);
}

TYPED_TEST(T256Test, OperatorSub) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    typename TestFixture::T256 expected{-2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f, -2.0f};
    auto v3 = v1 - v2;
    ASSERT_EQ(v3, expected);
}

TYPED_TEST(T256Test, OperatorMul) {
    typename TestFixture::T256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    typename TestFixture::T256 v2{3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    typename TestFixture::T256 expected{3.0f, 8.0f, 15.0f, 24.0f, 35.0f, 48.0f, 63.0f, 80.0f};
    auto v3 = v1 * v2;
    ASSERT_EQ(v3, expected);
}

TYPED_TEST(T256Test, OperatorDiv) {
    typename TestFixture::T256 v1{4.0f, 3.0f, 9.0f, 12.0f, 4.0f, 3.0f, 9.0f, 12.0f};
    typename TestFixture::T256 v2{1.0f, 2.0f, 3.0f, 3.0f, 1.0f, 2.0f, 3.0f, 3.0f};
    float expected[TestFixture::T256::size()] = {4.0f, 1.5f, 3.0f, 4.0f, 4.0f, 1.5f, 3.0f, 4.0f};
    auto v3 = v1 / v2;

    float result[TestFixture::T256::size()];
    v3.unalignedStore(result);

    ASSERT_ELEMENTS_NEAR(result, expected, TestFixture::T256::size(), 0.0001f);
}

TEST(T256Test, TransposeSquareScalar) {
    trimd::scalar::F256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v2{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v3{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v4{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v5{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v6{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v7{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::scalar::F256 v8{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    trimd::scalar::transpose(v1, v2, v3, v4, v5, v6, v7, v8);

    trimd::scalar::F256 e1{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    trimd::scalar::F256 e2{2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f};
    trimd::scalar::F256 e3{3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    trimd::scalar::F256 e4{4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f};
    trimd::scalar::F256 e5{5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f};
    trimd::scalar::F256 e6{6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f};
    trimd::scalar::F256 e7{7.0f, 7.0f, 7.0f, 7.0f, 7.0f, 7.0f, 7.0f, 7.0f};
    trimd::scalar::F256 e8{8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f};

    ASSERT_EQ(v1, e1);
    ASSERT_EQ(v2, e2);
    ASSERT_EQ(v3, e3);
    ASSERT_EQ(v4, e4);
    ASSERT_EQ(v5, e5);
    ASSERT_EQ(v6, e6);
    ASSERT_EQ(v7, e7);
    ASSERT_EQ(v8, e8);
}

#ifdef TRIMD_ENABLE_AVX
TEST(T256Test, TransposeSquareAVX) {
    trimd::avx::F256 v1{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v2{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v3{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v4{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v5{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v6{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v7{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    trimd::avx::F256 v8{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};

    trimd::avx::transpose(v1, v2, v3, v4, v5, v6, v7, v8);

    trimd::avx::F256 e1{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    trimd::avx::F256 e2{2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f};
    trimd::avx::F256 e3{3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f, 3.0f};
    trimd::avx::F256 e4{4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f};
    trimd::avx::F256 e5{5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f};
    trimd::avx::F256 e6{6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f, 6.0f};
    trimd::avx::F256 e7{7.0f, 7.0f, 7.0f, 7.0f, 7.0f, 7.0f, 7.0f, 7.0f};
    trimd::avx::F256 e8{8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f, 8.0f};

    ASSERT_EQ(v1, e1);
    ASSERT_EQ(v2, e2);
    ASSERT_EQ(v3, e3);
    ASSERT_EQ(v4, e4);
    ASSERT_EQ(v5, e5);
    ASSERT_EQ(v6, e6);
    ASSERT_EQ(v7, e7);
    ASSERT_EQ(v8, e8);
}
#endif  // TRIMD_ENABLE_AVX
