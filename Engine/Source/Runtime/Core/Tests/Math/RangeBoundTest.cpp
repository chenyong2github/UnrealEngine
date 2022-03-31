// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Math/Range.h"
#include "TestHarness.h"

TEST_CASE("Core::Math::FFloatRangeBound::Smoke Test", "[Core][Math][Smoke]")
{
	// ensure template instantiation works
	FDateRange DateRange;
	FDoubleRange DoubleRange;
	FFloatRange FloatRange;
	FInt8Range Int8Range;
	FInt16Range Int16Range;
	FInt32Range Int32Range;
	FInt64Range Int64Range;

	// bound types must be correct after construction
	FFloatRangeBound b1_1 = FFloatRangeBound::Exclusive(2.0f);
	FFloatRangeBound b1_2 = FFloatRangeBound::Inclusive(2.0f);
	FFloatRangeBound b1_3 = FFloatRangeBound::Open();
	FFloatRangeBound b1_4 = 2;

	CHECK(b1_1.IsExclusive());
	CHECK(b1_1.IsClosed());
	CHECK_FALSE(b1_1.IsInclusive());
	CHECK_FALSE(b1_1.IsOpen());
	CHECK_EQUAL(b1_1.GetValue(), 2.0f);

	CHECK(b1_2.IsInclusive());
	CHECK(b1_2.IsClosed());
	CHECK_FALSE(b1_2.IsExclusive());
	CHECK_FALSE(b1_2.IsOpen());
	CHECK_EQUAL(b1_2.GetValue(), 2.0f);

	CHECK(b1_3.IsOpen());
	CHECK_FALSE(b1_3.IsClosed());
	CHECK_FALSE(b1_3.IsExclusive());
	CHECK_FALSE(b1_3.IsInclusive());

	CHECK(b1_4.IsInclusive());
	CHECK_EQUAL(b1_4, b1_2);

	// comparisons must be correct
	FFloatRangeBound b2_1 = FFloatRangeBound::Exclusive(2.0f);
	FFloatRangeBound b2_2 = FFloatRangeBound::Exclusive(2.0f);
	FFloatRangeBound b2_3 = FFloatRangeBound::Inclusive(2.0f);
	FFloatRangeBound b2_4 = FFloatRangeBound::Inclusive(2.0f);
	FFloatRangeBound b2_5 = FFloatRangeBound::Open();
	FFloatRangeBound b2_6 = FFloatRangeBound::Open();

	CHECK(b2_1 == b2_2);
	CHECK(b2_3 == b2_4);
	CHECK(b2_5 == b2_6);

	CHECK_FALSE(b2_1 != b2_2);
	CHECK_FALSE(b2_3 != b2_4);
	CHECK_FALSE( b2_5 != b2_6);

	FFloatRangeBound b2_7 = FFloatRangeBound::Exclusive(3.0f);
	FFloatRangeBound b2_8 = FFloatRangeBound::Inclusive(3.0f);

	CHECK(b2_1 != b2_7);
	CHECK(b2_2 != b2_8);

	CHECK_FALSE(b2_1 == b2_7);
	CHECK_FALSE(b2_2 == b2_8);

	// min-max comparisons between bounds must be correct
	FFloatRangeBound b3_1 = FFloatRangeBound::Exclusive(2.0f);
	FFloatRangeBound b3_2 = FFloatRangeBound::Inclusive(2.0f);
	FFloatRangeBound b3_3 = FFloatRangeBound::Open();

	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_2, b3_1), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_1, b3_2), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_3, b3_1), b3_3);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_1, b3_3), b3_3);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_3, b3_2), b3_3);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_2, b3_3), b3_3);

	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_2, b3_1), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_1, b3_2), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_3, b3_1), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_1, b3_3), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_3, b3_2), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_2, b3_3), b3_2);

	CHECK_EQUAL(FFloatRangeBound::MinUpper(b3_2, b3_1), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MinUpper(b3_1, b3_2), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MinUpper(b3_3, b3_1), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MinUpper(b3_1, b3_3), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MinUpper(b3_3, b3_2), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MinUpper(b3_2, b3_3), b3_2);

	CHECK_EQUAL(FFloatRangeBound::MaxUpper(b3_2, b3_1), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MaxUpper(b3_1, b3_2), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MaxUpper(b3_3, b3_1), b3_3);
	CHECK_EQUAL(FFloatRangeBound::MaxUpper(b3_1, b3_3), b3_3);
	CHECK_EQUAL(FFloatRangeBound::MaxUpper(b3_3, b3_2), b3_3);
	CHECK_EQUAL( FFloatRangeBound::MaxUpper(b3_2, b3_3), b3_3);

	FFloatRangeBound b4_1 = FFloatRangeBound::Exclusive(3);
	FFloatRangeBound b4_2 = FFloatRangeBound::Inclusive(3);

	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_1, b4_2), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b4_2, b3_1), b3_1);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b3_2, b4_2), b3_2);
	CHECK_EQUAL(FFloatRangeBound::MinLower(b4_2, b3_2), b3_2);

	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_1, b4_2), b4_2);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b4_2, b3_1), b4_2);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b3_2, b4_2), b4_2);
	CHECK_EQUAL(FFloatRangeBound::MaxLower(b4_2, b3_2), b4_2);

}
