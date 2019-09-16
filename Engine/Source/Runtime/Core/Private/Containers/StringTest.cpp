// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/AssertionMacros.h"
#include "Containers/UnrealString.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringSanitizeFloatTest, "System.Core.String.SanitizeFloat", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringSanitizeFloatTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const double InVal, const int32 InMinFractionalDigits, const FString& InExpected)
	{
		const FString Result = FString::SanitizeFloat(InVal, InMinFractionalDigits);
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("%f (%d digits) failure: result '%s' (expected '%s')"), InVal, InMinFractionalDigits, *Result, *InExpected));
		}
	};

	DoTest(+0.0, 0, TEXT("0"));
	DoTest(-0.0, 0, TEXT("0"));

	DoTest(+100.0000, 0, TEXT("100"));
	DoTest(+100.1000, 0, TEXT("100.1"));
	DoTest(+100.1010, 0, TEXT("100.101"));
	DoTest(-100.0000, 0, TEXT("-100"));
	DoTest(-100.1000, 0, TEXT("-100.1"));
	DoTest(-100.1010, 0, TEXT("-100.101"));

	DoTest(+100.0000, 1, TEXT("100.0"));
	DoTest(+100.1000, 1, TEXT("100.1"));
	DoTest(+100.1010, 1, TEXT("100.101"));
	DoTest(-100.0000, 1, TEXT("-100.0"));
	DoTest(-100.1000, 1, TEXT("-100.1"));
	DoTest(-100.1010, 1, TEXT("-100.101"));

	DoTest(+100.0000, 4, TEXT("100.0000"));
	DoTest(+100.1000, 4, TEXT("100.1000"));
	DoTest(+100.1010, 4, TEXT("100.1010"));
	DoTest(-100.0000, 4, TEXT("-100.0000"));
	DoTest(-100.1000, 4, TEXT("-100.1000"));
	DoTest(-100.1010, 4, TEXT("-100.1010"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringAppendIntTest, "System.Core.String.AppendInt", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringAppendIntTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("'%s' failure: result '%s' (expected '%s')"), Call, *Result, InExpected));
		}
	};

	{
		FString Zero;
		Zero.AppendInt(0);
		DoTest(TEXT("AppendInt(0)"), Zero, TEXT("0"));
	}

	{
		FString IntMin;
		IntMin.AppendInt(MIN_int32);
		DoTest(TEXT("AppendInt(MIN_int32)"), IntMin, TEXT("-2147483648"));
	}

	{
		FString IntMin;
		IntMin.AppendInt(MAX_int32);
		DoTest(TEXT("AppendInt(MAX_int32)"), IntMin, TEXT("2147483647"));
	}

	{
		FString AppendMultipleInts;
		AppendMultipleInts.AppendInt(1);
		AppendMultipleInts.AppendInt(-2);
		AppendMultipleInts.AppendInt(3);
		DoTest(TEXT("AppendInt(1);AppendInt(-2);AppendInt(3)"), AppendMultipleInts, TEXT("1-23"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringUnicodeTest, "System.Core.String.Unicode", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringUnicodeTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("'%s' failure: result '%s' (expected '%s')"), Call, *Result, InExpected));
		}
	};

	// Test data used to verify basic processing of a Unicode character outside the BMP
	FString TestStr;
	if (FUnicodeChar::CodepointToString(128512, TestStr))
	{
		// Verify that the string can be serialized and deserialized without losing any data
		{
			TArray<uint8> StringData;
			FString FromArchive = TestStr;

			FMemoryWriter Writer(StringData);
			Writer << FromArchive;

			FromArchive.Reset();
			FMemoryReader Reader(StringData);
			Reader << FromArchive;

			DoTest(TEXT("FromArchive"), FromArchive, *TestStr);
		}

		// Verify that the string can be converted from/to UTF-8 without losing any data
		{
			const FString FromUtf8 = UTF8_TO_TCHAR(TCHAR_TO_UTF8(*TestStr));
			DoTest(TEXT("FromUtf8"), FromUtf8, *TestStr);
		}

		// Verify that the string can be converted from/to UTF-16 without losing any data
		{
			const FString FromUtf16 = UTF16_TO_TCHAR(TCHAR_TO_UTF16(*TestStr));
			DoTest(TEXT("FromUtf16"), FromUtf16, *TestStr);
		}
	}


	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
