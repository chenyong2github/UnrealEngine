// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Misc/Timespan.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Misc::FTimespan::Smoke Test", "[Core][Misc][Smoke]")
{
	// constructors must create equal objects
	{
		FTimespan ts1_1 = FTimespan(3, 2, 1);
		FTimespan ts1_2 = FTimespan(0, 3, 2, 1);
		FTimespan ts1_3 = FTimespan(0, 3, 2, 1, 0);

		TEST_EQUAL(TEXT("Constructors must create equal objects (Hours/Minutes/Seconds vs. Days/Hours/Minutes/Seconds)"), ts1_1, ts1_2);
		TEST_EQUAL(TEXT("Constructors must create equal objects (Hours/Minutes/Seconds vs. Days/Hours/Minutes/Seconds/FractionNano)"), ts1_1, ts1_3);
	}

	// component getters must return correct values
	{
		FTimespan ts2_1 = FTimespan(1, 2, 3, 4, 123456789);

		TEST_EQUAL(TEXT("Component getters must return correct values (Days)"), ts2_1.GetDays(), 1);
		TEST_EQUAL(TEXT("Component getters must return correct values (Hours)"), ts2_1.GetHours(), 2);
		TEST_EQUAL(TEXT("Component getters must return correct values (Minutes)"), ts2_1.GetMinutes(), 3);
		TEST_EQUAL(TEXT("Component getters must return correct values (Seconds)"), ts2_1.GetSeconds(), 4);
		TEST_EQUAL(TEXT("Component getters must return correct values (FractionMilli)"), ts2_1.GetFractionMilli(), 123);
		TEST_EQUAL(TEXT("Component getters must return correct values (FractionMicro)"), ts2_1.GetFractionMicro(), 123456);
		TEST_EQUAL(TEXT("Component getters must return correct values (FractionNano)"), ts2_1.GetFractionNano(), 123456700);
	}

	// durations of positive and negative time spans must match
	{
		FTimespan ts3_1 = FTimespan(1, 2, 3, 4, 123456789);
		FTimespan ts3_2 = FTimespan(-1, -2, -3, -4, -123456789);

		TEST_EQUAL(TEXT("Durations of positive and negative time spans must match"), ts3_1.GetDuration(), ts3_2.GetDuration());
	}

	// static constructors must create correct values
	{
		TEST_EQUAL(TEXT("Static constructors must create correct values (FromDays)"), FTimespan::FromDays(123).GetTotalDays(), 123.0);
		TEST_EQUAL(TEXT("Static constructors must create correct values (FromHours)"), FTimespan::FromHours(123).GetTotalHours(), 123.0);
		TEST_EQUAL(TEXT("Static constructors must create correct values (FromMinutes)"), FTimespan::FromMinutes(123).GetTotalMinutes(), 123.0);
		TEST_EQUAL(TEXT("Static constructors must create correct values (FromSeconds)"), FTimespan::FromSeconds(123).GetTotalSeconds(), 123.0);
		TEST_EQUAL(TEXT("Static constructors must create correct values (FromMilliseconds)"), FTimespan::FromMilliseconds(123).GetTotalMilliseconds(), 123.0);
		TEST_EQUAL(TEXT("Static constructors must create correct values (FromMicroseconds)"), FTimespan::FromMicroseconds(123).GetTotalMicroseconds(), 123.0);
	}

	// string conversions must return correct strings
	{
		FTimespan ts5_1 = FTimespan(1, 2, 3, 4, 123456789);

		TEST_TRUE(TEXT("String conversion (Default)"), !FCString::Strcmp(*ts5_1.ToString(), TEXT("+1.02:03:04.123")));
		TEST_TRUE(TEXT("String conversion (%d.%h:%m:%s.%f)"), !FCString::Strcmp(*ts5_1.ToString(TEXT("%d.%h:%m:%s.%f")), TEXT("+1.02:03:04.123")));
		TEST_TRUE(TEXT("String conversion (%d.%h:%m:%s.%u)"), !FCString::Strcmp(*ts5_1.ToString(TEXT("%d.%h:%m:%s.%u")), TEXT("+1.02:03:04.123456")));
		TEST_TRUE(TEXT("String conversion (%D.%h:%m:%s.%n)"), !FCString::Strcmp(*ts5_1.ToString(TEXT("%D.%h:%m:%s.%n")), TEXT("+00000001.02:03:04.123456700")));
	}

	// parsing valid strings must succeed
	{
		FTimespan ts6_t;

		FTimespan ts6_1 = FTimespan(1, 2, 3, 4, 123000000);
		FTimespan ts6_2 = FTimespan(1, 2, 3, 4, 123456000);
		FTimespan ts6_3 = FTimespan(1, 2, 3, 4, 123456700);

		TEST_TRUE(TEXT("Parsing valid strings must succeed (+1.02:03:04.123)"), FTimespan::Parse(TEXT("+1.02:03:04.123"), ts6_t));
		TEST_EQUAL(TEXT("Parsing valid strings must result in correct values (+1.02:03:04.123)"), ts6_t, ts6_1);

		TEST_TRUE(TEXT("Parsing valid strings must succeed (+1.02:03:04.123456)"), FTimespan::Parse(TEXT("+1.02:03:04.123456"), ts6_t));
		TEST_EQUAL(TEXT("Parsing valid strings must result in correct values (+1.02:03:04.123456)"), ts6_t, ts6_2);

		TEST_TRUE(TEXT("Parsing valid strings must succeed (+1.02:03:04.123456789)"), FTimespan::Parse(TEXT("+1.02:03:04.123456789"), ts6_t));
		TEST_EQUAL(TEXT("Parsing valid strings must result in correct values (+1.02:03:04.123456789)"), ts6_t, ts6_3);

		FTimespan ts6_4 = FTimespan(-1, -2, -3, -4, -123000000);
		FTimespan ts6_5 = FTimespan(-1, -2, -3, -4, -123456000);
		FTimespan ts6_6 = FTimespan(-1, -2, -3, -4, -123456700);

		TEST_TRUE(TEXT("Parsing valid strings must succeed (-1.02:03:04.123)"), FTimespan::Parse(TEXT("-1.02:03:04.123"), ts6_t));
		TEST_EQUAL(TEXT("Parsing valid strings must result in correct values (-1.02:03:04.123)"), ts6_t, ts6_4);

		TEST_TRUE(TEXT("Parsing valid strings must succeed (-1.02:03:04.123456)"), FTimespan::Parse(TEXT("-1.02:03:04.123456"), ts6_t));
		TEST_EQUAL(TEXT("Parsing valid strings must result in correct values (-1.02:03:04.123456)"), ts6_t, ts6_5);

		TEST_TRUE(TEXT("Parsing valid strings must succeed (-1.02:03:04.123456789)"), FTimespan::Parse(TEXT("-1.02:03:04.123456789"), ts6_t));
		TEST_EQUAL(TEXT("Parsing valid strings must result in correct values (-1.02:03:04.123456789)"), ts6_t, ts6_6);
	}

	// parsing invalid strings must fail
	{
		FTimespan ts7_1;

		//TEST_FALSE(TEXT("Parsing invalid strings must fail (1,02:03:04.005)"), FTimespan::Parse(TEXT("1,02:03:04.005"), ts7_1));
		//TEST_FALSE(TEXT("Parsing invalid strings must fail (1.1.02:03:04:005)"), FTimespan::Parse(TEXT("1.1.02:03:04:005"), ts7_1));
		//TEST_FALSE(TEXT("Parsing invalid strings must fail (04:005)"), FTimespan::Parse(TEXT("04:005"), ts7_1));
	}

	// `From*` converters must return correct value
	// test normal and edge cases for polar conversions (FromMicroseconds() and FromDay()) and just normal case for all others
	{
		TEST_EQUAL(TEXT("FromMicroseconds(0) results in correct value"), FTimespan::FromMicroseconds(0), FTimespan(0));
		TEST_EQUAL(TEXT("FromMicroseconds(1) results in correct value"), FTimespan::FromMicroseconds(1), FTimespan(1 * ETimespan::TicksPerMicrosecond));
		TEST_EQUAL(TEXT("FromMicroseconds(1.1) results in correct value"), FTimespan::FromMicroseconds(1.1), FTimespan(1 * ETimespan::TicksPerMicrosecond + 1));
		TEST_EQUAL(TEXT("FromMicroseconds(1.5) results in correct value"), FTimespan::FromMicroseconds(1.5), FTimespan(1 * ETimespan::TicksPerMicrosecond + 5));
		TEST_EQUAL(TEXT("FromMicroseconds(1.499999999999997) results in 1.5 microsecs of ticks"), FTimespan::FromMicroseconds(1.499999999999997), FTimespan(1 * ETimespan::TicksPerMicrosecond + 5));
		TEST_EQUAL(TEXT("FromMicroseconds(1.50000001) results in  in 1.5 microsecs of ticks"), FTimespan::FromMicroseconds(1.50000001), FTimespan(1 * ETimespan::TicksPerMicrosecond + 5));
		TEST_EQUAL(TEXT("FromMicroseconds(-1) results in correct value"), FTimespan::FromMicroseconds(-1), FTimespan(-1 * ETimespan::TicksPerMicrosecond));
		TEST_EQUAL(TEXT("FromMicroseconds(-1.1) results in correct value"), FTimespan::FromMicroseconds(-1.1), FTimespan(-1 * ETimespan::TicksPerMicrosecond - 1));
		TEST_EQUAL(TEXT("FromMicroseconds(-1.5) results in correct value"), FTimespan::FromMicroseconds(-1.5), FTimespan(-1 * ETimespan::TicksPerMicrosecond - 5));

		TEST_EQUAL(TEXT("FromMilliseconds(1) results in correct value"), FTimespan::FromMilliseconds(1), FTimespan(1 * ETimespan::TicksPerMillisecond));
		TEST_EQUAL(TEXT("FromSeconds(1) results in correct value"), FTimespan::FromSeconds(1), FTimespan(1 * ETimespan::TicksPerSecond));
		TEST_EQUAL(TEXT("FromMinutes(1) results in correct value"), FTimespan::FromMinutes(1), FTimespan(1 * ETimespan::TicksPerMinute));
		TEST_EQUAL(TEXT("FromHours(1) results in correct value"), FTimespan::FromHours(1), FTimespan(1 * ETimespan::TicksPerHour));

		TEST_EQUAL(TEXT("FromDays(0) results in correct value"), FTimespan::FromDays(0), FTimespan(0));
		TEST_EQUAL(TEXT("FromDays(1) results in correct value"), FTimespan::FromDays(1), FTimespan(1 * ETimespan::TicksPerDay));
		TEST_EQUAL(TEXT("FromDays(1.25) results in correct value (1 day and 6 hours of ticks)"), FTimespan::FromDays(1.25), FTimespan(1 * ETimespan::TicksPerDay + 6 * ETimespan::TicksPerHour));
		TEST_EQUAL(TEXT("FromDays(1.5) results in correct value (1 day and 12 hours of ticks)"), FTimespan::FromDays(1.5), FTimespan(1 * ETimespan::TicksPerDay + 12 * ETimespan::TicksPerHour));
		TEST_EQUAL(TEXT("FromDays(1.499999999999997) results in correct value (1 day and 12 hours of ticks)"), FTimespan::FromDays(1.499999999999997), FTimespan(1 * ETimespan::TicksPerDay + 12 * ETimespan::TicksPerHour));
		TEST_EQUAL(TEXT("FromDays(-1) results in correct value"), FTimespan::FromDays(-1), FTimespan(-1 * ETimespan::TicksPerDay));
		TEST_EQUAL(TEXT("FromDays(-1.25) results in correct value (minus 1 day and 6 hours of ticks)"), FTimespan::FromDays(-1.25), FTimespan(-1 * ETimespan::TicksPerDay - 6 * ETimespan::TicksPerHour));
		TEST_EQUAL(TEXT("FromDays(-1.5) results in correct value (minus 1 day and 12 hours of ticks)"), FTimespan::FromDays(-1.5), FTimespan(-1 * ETimespan::TicksPerDay - 12 * ETimespan::TicksPerHour));
	}
}
