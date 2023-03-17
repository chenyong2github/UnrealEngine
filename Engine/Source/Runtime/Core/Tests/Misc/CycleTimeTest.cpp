// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "Misc/CycleTime.h"

#include "Containers/UnrealString.h"
#include "Misc/Timespan.h"
#include "TestHarness.h"

namespace UE
{

TEST_CASE("Core::Time::CycleTimeSpan", "[Core][Time][Smoke]")
{
	SECTION("Constructors")
	{
		STATIC_CHECK(FCycleTimeSpan().GetCycles() == 0);
		STATIC_CHECK(FCycleTimeSpan::FromCycles(0).GetCycles() == 0);
		STATIC_CHECK(FCycleTimeSpan::FromCycles(123).GetCycles() == 123);
		STATIC_CHECK(FCycleTimeSpan::Zero().GetCycles() == 0);
	}

	SECTION("Comparison")
	{
		STATIC_CHECK(FCycleTimeSpan::FromCycles(0) == FCycleTimeSpan());
		STATIC_CHECK(FCycleTimeSpan::FromCycles(1) != FCycleTimeSpan());
		STATIC_CHECK(FCycleTimeSpan::FromCycles(0) <= FCycleTimeSpan());
		STATIC_CHECK(FCycleTimeSpan::FromCycles(0) >= FCycleTimeSpan());
		STATIC_CHECK(FCycleTimeSpan::FromCycles(0) < FCycleTimeSpan::FromCycles(1));
		STATIC_CHECK(FCycleTimeSpan::FromCycles(1) > FCycleTimeSpan::FromCycles(0));
	}

	SECTION("Infinity")
	{
		STATIC_CHECK(FCycleTimeSpan::Infinity().IsInfinity());
		STATIC_CHECK_FALSE(FCycleTimeSpan().IsInfinity());
		STATIC_CHECK_FALSE(FCycleTimeSpan::FromCycles(123).IsInfinity());

		STATIC_CHECK(FCycleTimeSpan::Infinity() == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() <= FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() >= FCycleTimeSpan::Infinity());

		STATIC_CHECK(FCycleTimeSpan::Infinity() >= FCycleTimeSpan());
		STATIC_CHECK(FCycleTimeSpan::Infinity() > FCycleTimeSpan());

		STATIC_CHECK(FCycleTimeSpan() != FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan() <= FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan() < FCycleTimeSpan::Infinity());
	}

	SECTION("Addition")
	{
		STATIC_CHECK(FCycleTimeSpan::FromCycles(234) + FCycleTimeSpan::FromCycles(123) == FCycleTimeSpan::FromCycles(357));
		STATIC_CHECK(FCycleTimeSpan::FromCycles(234) + (-FCycleTimeSpan::Infinity()) == -FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::FromCycles(234) + FCycleTimeSpan::Infinity() == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() + FCycleTimeSpan::FromCycles(1) == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() + (-FCycleTimeSpan::Infinity()) == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() + FCycleTimeSpan::Infinity() == FCycleTimeSpan::Infinity());
		STATIC_CHECK((-FCycleTimeSpan::Infinity()) + FCycleTimeSpan::FromCycles(1) == -FCycleTimeSpan::Infinity());
		STATIC_CHECK((-FCycleTimeSpan::Infinity()) + (-FCycleTimeSpan::Infinity()) == -FCycleTimeSpan::Infinity());
		STATIC_CHECK((-FCycleTimeSpan::Infinity()) + FCycleTimeSpan::Infinity() == -FCycleTimeSpan::Infinity());
	}

	SECTION("Subtraction")
	{
		STATIC_CHECK(FCycleTimeSpan::FromCycles(234) - FCycleTimeSpan::FromCycles(123) == FCycleTimeSpan::FromCycles(111));
		STATIC_CHECK(FCycleTimeSpan::FromCycles(234) - (-FCycleTimeSpan::Infinity()) == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::FromCycles(234) - FCycleTimeSpan::Infinity() == -FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() - FCycleTimeSpan::FromCycles(1) == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() - (-FCycleTimeSpan::Infinity()) == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimeSpan::Infinity() - FCycleTimeSpan::Infinity() == FCycleTimeSpan::Infinity());
		STATIC_CHECK((-FCycleTimeSpan::Infinity()) - FCycleTimeSpan::FromCycles(1) == -FCycleTimeSpan::Infinity());
		STATIC_CHECK((-FCycleTimeSpan::Infinity()) - (-FCycleTimeSpan::Infinity()) == -FCycleTimeSpan::Infinity());
		STATIC_CHECK((-FCycleTimeSpan::Infinity()) - FCycleTimeSpan::Infinity() == -FCycleTimeSpan::Infinity());
	}

	SECTION("Conversions")
	{
		CHECK(FMath::IsNearlyEqual(FCycleTimeSpan::FromSeconds(123.0).ToSeconds(), 123.0));
		CHECK(FMath::IsNearlyEqual(FCycleTimeSpan::FromMilliseconds(123.0).ToMilliseconds(), 123.0));
		CHECK(FMath::IsNearlyEqual(FCycleTimeSpan(FTimespan::FromSeconds(123.0)).ToSeconds(), 123.0));
		CHECK(FMath::IsNearlyEqual(FCycleTimeSpan(FTimespan::FromSeconds(-123.0)).ToSeconds(), -123.0));
	}
}

TEST_CASE("Core::Time::CycleTimePoint", "[Core][Time][Smoke]")
{
	SECTION("Constructors")
	{
		STATIC_CHECK(FCycleTimePoint().GetCycles() == 0);
		STATIC_CHECK(FCycleTimePoint::FromCycles(0).GetCycles() == 0);
		STATIC_CHECK(FCycleTimePoint::FromCycles(123).GetCycles() == 123);
	}

	SECTION("Comparison")
	{
		STATIC_CHECK(FCycleTimePoint::FromCycles(0) == FCycleTimePoint());
		STATIC_CHECK(FCycleTimePoint::FromCycles(1) != FCycleTimePoint());
		STATIC_CHECK(FCycleTimePoint::FromCycles(0) <= FCycleTimePoint());
		STATIC_CHECK(FCycleTimePoint::FromCycles(0) >= FCycleTimePoint());
		STATIC_CHECK(FCycleTimePoint::FromCycles(0) < FCycleTimePoint::FromCycles(1));
		STATIC_CHECK(FCycleTimePoint::FromCycles(1) > FCycleTimePoint::FromCycles(0));
	}

	SECTION("Infinity")
	{
		STATIC_CHECK(FCycleTimePoint::Infinity().IsInfinity());
		STATIC_CHECK_FALSE(FCycleTimePoint().IsInfinity());
		STATIC_CHECK_FALSE(FCycleTimePoint::FromCycles(123).IsInfinity());

		STATIC_CHECK(FCycleTimePoint::Infinity() == FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint::Infinity() <= FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint::Infinity() >= FCycleTimePoint::Infinity());

		STATIC_CHECK(FCycleTimePoint::Infinity() >= FCycleTimePoint());
		STATIC_CHECK(FCycleTimePoint::Infinity() > FCycleTimePoint());

		STATIC_CHECK(FCycleTimePoint() != FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint() <= FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint() < FCycleTimePoint::Infinity());
	}

	SECTION("Addition")
	{
		STATIC_CHECK(FCycleTimePoint::FromCycles(234) + FCycleTimeSpan::FromCycles(123) == FCycleTimePoint::FromCycles(357));
		STATIC_CHECK(FCycleTimePoint::Infinity() + FCycleTimeSpan::FromCycles(1) == FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint::Infinity() + (-FCycleTimeSpan::Infinity()) == FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint::Infinity() + FCycleTimeSpan::Infinity() == FCycleTimePoint::Infinity());
	}

	SECTION("Subtraction")
	{
		STATIC_CHECK(FCycleTimePoint::FromCycles(234) - FCycleTimeSpan::FromCycles(123) == FCycleTimePoint::FromCycles(111));
		STATIC_CHECK(FCycleTimePoint::Infinity() - FCycleTimeSpan::FromCycles(1) == FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint::Infinity() - (-FCycleTimeSpan::Infinity()) == FCycleTimePoint::Infinity());
		STATIC_CHECK(FCycleTimePoint::Infinity() - FCycleTimeSpan::Infinity() == FCycleTimePoint::Infinity());
	}

	SECTION("Span")
	{
		STATIC_CHECK(FCycleTimePoint::FromCycles(357) - FCycleTimePoint::FromCycles(234) == FCycleTimeSpan::FromCycles(123));
		STATIC_CHECK(FCycleTimePoint::FromCycles(234) - FCycleTimePoint::FromCycles(357) == FCycleTimeSpan::FromCycles(-123));

		STATIC_CHECK(FCycleTimePoint::Infinity() - FCycleTimePoint::FromCycles(123) == FCycleTimeSpan::Infinity());
		STATIC_CHECK(FCycleTimePoint::FromCycles(123) - FCycleTimePoint::Infinity() == -FCycleTimeSpan::Infinity());
	}
}

} // namespace UE

#endif // WITH_LOW_LEVEL_TESTS
