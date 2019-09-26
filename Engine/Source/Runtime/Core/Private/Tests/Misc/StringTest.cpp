// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringTest, "System.Core.Misc.String", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FStringTest::RunTest( const FString& Parameters )
{
	// Test that LexFromString can intepret all the numerical formats we expect it to
	{
		// Test float values

		float Value;

		// Basic numbers
		TestTrue(TEXT("(float conversion from string) 1"), LexTryParseString(Value, (TEXT("1"))) && Value == 1);
		TestTrue(TEXT("(float conversion from string) 1.0"), LexTryParseString(Value, (TEXT("1.0"))) && Value == 1);
		TestTrue(TEXT("(float conversion from string) .1"), LexTryParseString(Value, (TEXT(".5"))) && Value == 0.5);
		TestTrue(TEXT("(float conversion from string) 1."), LexTryParseString(Value, (TEXT("1."))) && Value == 1);

		// Variations of 0
		TestTrue(TEXT("(float conversion from string) 0"), LexTryParseString(Value, (TEXT("0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) -0"), LexTryParseString(Value, (TEXT("-0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) 0.0"), LexTryParseString(Value, (TEXT("0.0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) .0"), LexTryParseString(Value, (TEXT(".0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) 0."), LexTryParseString(Value, (TEXT("0."))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) 0. 111"), LexTryParseString(Value, (TEXT("0. 111"))) && Value == 0);

		// Scientific notation
		TestTrue(TEXT("(float conversion from string) 1.0e+10"), LexTryParseString(Value, (TEXT("1.0e+10"))) && Value == 1.0e+10f);
		TestTrue(TEXT("(float conversion from string) 1.0e-10"), LexTryParseString(Value, (TEXT("1.99999999e-11"))) && Value == 1.99999999e-11f);
		TestTrue(TEXT("(float conversion from string) 1e+10"), LexTryParseString(Value, (TEXT("1e+10"))) && Value == 1e+10f);

		// Float as hex

		// Non-finite special numbers
		TestTrue(TEXT("(float conversion from string) inf"), LexTryParseString(Value, (TEXT("inf"))));
		TestTrue(TEXT("(float conversion from string) nan"), LexTryParseString(Value, (TEXT("nan"))));
		TestTrue(TEXT("(float conversion from string) nan(ind)"), LexTryParseString(Value, (TEXT("nan(ind)"))));

		// nan/inf etc. are detected from the start of the string, regardless of any other characters that come afterwards
		TestTrue(TEXT("(float conversion from string) nananananananana"), LexTryParseString(Value, (TEXT("nananananananana"))));
		TestTrue(TEXT("(float conversion from string) nan(ind)!"), LexTryParseString(Value, (TEXT("nan(ind)!"))));
		TestTrue(TEXT("(float conversion from string) infinity"), LexTryParseString(Value, (TEXT("infinity"))));

		// Some numbers with whitespace
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("   2.5   "))) && Value == 2.5);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\t3.0\t"))) && Value == 3.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("4.0   \t"))) && Value == 4.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\r\n5.25"))) && Value == 5.25);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 6 . 2 "))) && Value == 6.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 56 . 2 "))) && Value == 56.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 5 6 . 2 "))) && Value == 5.0);

		// Failure cases
		TestFalse(TEXT("(float no conversion from string) not a number"), LexTryParseString(Value, (TEXT("not a number"))));
		TestFalse(TEXT("(float no conversion from string) <empty string>"), LexTryParseString(Value, (TEXT(""))));
		TestFalse(TEXT("(float conversion from string) ."), LexTryParseString(Value, (TEXT("."))));
	}

	{
		// Test integer values

		int32 Value;

		// Basic numbers
		TestTrue(TEXT("(int32 conversion from string) 1"), LexTryParseString(Value, (TEXT("1"))) && Value == 1);
		TestTrue(TEXT("(int32 conversion from string) 1.0"), LexTryParseString(Value, (TEXT("1.0"))) && Value == 1);
		TestTrue(TEXT("(int32 conversion from string) 3.1"), LexTryParseString(Value, (TEXT("3.1"))) && Value == 3);
		TestTrue(TEXT("(int32 conversion from string) 0.5"), LexTryParseString(Value, (TEXT("0.5"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) 1."), LexTryParseString(Value, (TEXT("1."))) && Value == 1);

		// Variations of 0
		TestTrue(TEXT("(int32 conversion from string) 0"), LexTryParseString(Value, (TEXT("0"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) 0.0"), LexTryParseString(Value, (TEXT("0.0"))) && Value == 0);
		TestFalse(TEXT("(int32 conversion from string) .0"), LexTryParseString(Value, (TEXT(".0"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) 0."), LexTryParseString(Value, (TEXT("0."))) && Value == 0);

		// Scientific notation
		TestTrue(TEXT("(int32 conversion from string) 1.0e+10"), LexTryParseString(Value, (TEXT("1.0e+10"))));
		TestTrue(TEXT("(int32 conversion from string) 1.0e-10"), LexTryParseString(Value, (TEXT("1.0e-10"))));
		TestTrue(TEXT("(int32 conversion from string) 1.0e+10"), LexTryParseString(Value, (TEXT("0.0e+10"))));
		TestTrue(TEXT("(int32 conversion from string) 1.0e-10"), LexTryParseString(Value, (TEXT("0.0e-10"))));
		TestTrue(TEXT("(int32 conversion from string) 1e+10"), LexTryParseString(Value, (TEXT("1e+10"))));
		TestTrue(TEXT("(int32 conversion from string) 1e-10"), LexTryParseString(Value, (TEXT("1e-10"))));

		// Float as hex
		TestTrue(TEXT("(int32 conversion from string) 1.0e+10"), LexTryParseString(Value, (TEXT("1.0e+10"))));

		// Some numbers with whitespace
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("   2.5   "))) && Value == 2.5);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\t3.0\t"))) && Value == 3.0);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("4.0   \t"))) && Value == 4.0);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\r\n5.25"))) && Value == 5.25);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 6 . 2 "))) && Value == 6.0);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 56 . 2 "))) && Value == 56.0);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 5 6 . 2 "))) && Value == 5.0);

		// Non-finite special numbers. All shouldn't parse into an int
		TestFalse(TEXT("(int32 no conversion from string) inf"), LexTryParseString(Value, (TEXT("inf"))));
		TestFalse(TEXT("(int32 no conversion from string) nan"), LexTryParseString(Value, (TEXT("nan"))));
		TestFalse(TEXT("(int32 no conversion from string) nan(ind)"), LexTryParseString(Value, (TEXT("nan(ind)"))));
		TestFalse(TEXT("(int32 no conversion from string) nananananananana"), LexTryParseString(Value, (TEXT("nananananananana"))));
		TestFalse(TEXT("(int32 no conversion from string) nan(ind)!"), LexTryParseString(Value, (TEXT("nan(ind)!"))));
		TestFalse(TEXT("(int32 no conversion from string) infinity"), LexTryParseString(Value, (TEXT("infinity"))));
		TestFalse(TEXT("(float conversion from string) ."), LexTryParseString(Value, (TEXT("."))));
		TestFalse(TEXT("(float conversion from string) <empyty string>"), LexTryParseString(Value, (TEXT(""))));
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
