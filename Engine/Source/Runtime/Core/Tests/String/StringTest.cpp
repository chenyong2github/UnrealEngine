// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/AssertionMacros.h"
#include "Misc/StringBuilder.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::SanitizeFloat", "[Core][String][Smoke]")
{
	auto DoTest = [this](const double InVal, const int32 InMinFractionalDigits, const FString& InExpected)
	{
		const FString Result = FString::SanitizeFloat(InVal, InMinFractionalDigits);
		CHECK_EQUAL(Result, InExpected);
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
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::AppendInt", "[Core][String][Smoke]")
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		INFO(Call);
		CHECK_EQUAL(Result, InExpected);
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
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::Unicode", "[Core][String][Smoke]")
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		INFO(Call);
		CHECK_EQUAL(Result, InExpected);
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
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::LexTryParseString", "[Core][String][Smoke]")
{
	// Test that LexFromString can intepret all the numerical formats we expect it to

	SECTION("Test float values")
	{
		float Value;

		SECTION("Basic numbers")
		{
			CHECK(LexTryParseString(Value, TEXT("1")));
			CHECK(Value == 1);
			CHECK(LexTryParseString(Value, TEXT("1.0")));
			CHECK(Value == 1);
			CHECK(LexTryParseString(Value, TEXT(".5")));
			CHECK(Value == 0.5);
			CHECK(LexTryParseString(Value, TEXT("1.")));
			CHECK(Value == 1);
		}

		SECTION("Variations of 0")
		{
			CHECK(LexTryParseString(Value, TEXT("0")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("-0")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("0.0")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT(".0")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("0.")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("0. 111")));
			CHECK(Value == 0);
		}

		SECTION("Scientific notation")
		{
			CHECK(LexTryParseString(Value, TEXT("1.0e+10")));
			CHECK(Value == 1.0e+10f);
			CHECK(LexTryParseString(Value, TEXT("1.99999999e-11")));
			CHECK(Value == 1.99999999e-11f);
			CHECK(LexTryParseString(Value, TEXT("1e+10")));
			CHECK(Value == 1e+10f);
		}

		SECTION("Non-finite special numbers")
		{
			CHECK(LexTryParseString(Value, TEXT("inf")));
			CHECK(LexTryParseString(Value, TEXT("nan")));
			CHECK(LexTryParseString(Value, TEXT("nan(ind)")));
		}

		SECTION("nan/inf etc. are detected from the start of the string, regardless of any other characters that come afterwards")
		{
			CHECK(LexTryParseString(Value, TEXT("nananananananana")));
			CHECK(LexTryParseString(Value, TEXT("nan(ind)!")));
			CHECK(LexTryParseString(Value, TEXT("infinity")));
		}

		SECTION("Some numbers with whitespace")
		{	
			CHECK(LexTryParseString(Value, TEXT("   2.5   ")));
			CHECK(Value == 2.5);
			CHECK(LexTryParseString(Value, TEXT("\t3.0\t")));
			CHECK(Value == 3.0);
			CHECK(LexTryParseString(Value, TEXT("4.0   \t")));
			CHECK(Value == 4.0);
			CHECK(LexTryParseString(Value, TEXT("\r\n5.25")));
			CHECK(Value == 5.25);
			CHECK(LexTryParseString(Value, TEXT(" 6 . 2 ")));
			CHECK(Value == 6.0);
			CHECK(LexTryParseString(Value, TEXT(" 56 . 2 ")));
			CHECK(Value == 56.0);
			CHECK(LexTryParseString(Value, TEXT(" 5 6 . 2 ")));
			CHECK(Value == 5.0);
		}
		
		SECTION("Failure cases")
		{
			CHECK_FALSE(LexTryParseString(Value, TEXT("not a number")));
			CHECK_FALSE(LexTryParseString(Value, TEXT("")));
			CHECK_FALSE(LexTryParseString(Value, TEXT(".")));
		}
		
	}

	SECTION("Test integer values")
	{
		int32 Value;

		SECTION("Basic numbers")
		{
			CHECK(LexTryParseString(Value, TEXT("1")));
			CHECK(Value == 1);
			CHECK(LexTryParseString(Value, TEXT("1.0")));
			CHECK(Value == 1);
			CHECK(LexTryParseString(Value, TEXT("3.1")));
			CHECK(Value == 3);
			CHECK(LexTryParseString(Value, TEXT("0.5")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("1.")));
			CHECK(Value == 1);
		}

		SECTION("Variations of 0")
		{	
			CHECK(LexTryParseString(Value, TEXT("0")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("0.0")));
			CHECK(Value == 0);
			CHECK_FALSE(LexTryParseString(Value, TEXT(".0")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("0.")));
			CHECK(Value == 0);
		}

		SECTION("Scientific notation")
		{
			CHECK(LexTryParseString(Value, TEXT("1.0e+10")));
			CHECK(Value == 1);
			CHECK(LexTryParseString(Value, TEXT("6.0e-10")));
			CHECK(Value == 6);
			CHECK(LexTryParseString(Value, TEXT("0.0e+10")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("0.0e-10")));
			CHECK(Value == 0);
			CHECK(LexTryParseString(Value, TEXT("3e+10")));
			CHECK(Value == 3);
			CHECK(LexTryParseString(Value, TEXT("4e-10")));
			CHECK(Value == 4);
		}

		SECTION("Some numbers with whitespace")
		{
			CHECK(LexTryParseString(Value, TEXT("   2.5   ")));
			CHECK(Value == 2);
			CHECK(LexTryParseString(Value, TEXT("\t3.0\t")));
			CHECK(Value == 3);
			CHECK(LexTryParseString(Value, TEXT("4.0   \t")));
			CHECK(Value == 4);
			CHECK(LexTryParseString(Value, TEXT("\r\n5.25")));
			CHECK(Value == 5);
			CHECK(LexTryParseString(Value, TEXT(" 6 . 2 ")));
			CHECK(Value == 6);
			CHECK(LexTryParseString(Value, TEXT(" 56 . 2 ")));
			CHECK(Value == 56);
			CHECK(LexTryParseString(Value, TEXT(" 5 6 . 2 ")));
			CHECK(Value == 5);
		}
		
		SECTION("Non-finite special numbers. All shouldn't parse into an int")
		{
			CHECK_FALSE(LexTryParseString(Value, (TEXT("inf"))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT("nan"))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT("nan(ind)"))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT("nananananananana"))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT("nan(ind)!"))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT("infinity"))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT("."))));
			CHECK_FALSE(LexTryParseString(Value, (TEXT(""))));
		}
		
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::Substring", "[Core][String][Smoke]")
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		INFO(Call);
		CHECK_EQUAL(Result, InExpected);
	};

	const FString TestString(TEXT("0123456789"));

#define SUBSTRINGTEST(TestName, ExpectedResult, Operation, ...) \
	FString TestName = TestString.Operation(__VA_ARGS__); \
	DoTest(TEXT(#TestName), TestName, ExpectedResult); \
	\
	FString Inline##TestName = TestString; \
	Inline##TestName.Operation##Inline(__VA_ARGS__); \
	DoTest(TEXT("Inline" #TestName), Inline##TestName, ExpectedResult); 

	SECTION("Left")
	{
		SUBSTRINGTEST(Left, TEXT("0123"), Left, 4);
		SUBSTRINGTEST(ExactLengthLeft, *TestString, Left, 10);
		SUBSTRINGTEST(LongerThanLeft, *TestString, Left, 20);
		SUBSTRINGTEST(ZeroLeft, TEXT(""), Left, 0);
		SUBSTRINGTEST(NegativeLeft, TEXT(""), Left, -1);
	}

	SECTION("LeftChop")
	{
		SUBSTRINGTEST(LeftChop, TEXT("012345"), LeftChop, 4);
		SUBSTRINGTEST(ExactLengthLeftChop, TEXT(""), LeftChop, 10);
		SUBSTRINGTEST(LongerThanLeftChop, TEXT(""), LeftChop, 20);
		SUBSTRINGTEST(ZeroLeftChop, *TestString, LeftChop, 0);
		SUBSTRINGTEST(NegativeLeftChop, *TestString, LeftChop, -1);
	}

	SECTION("Right")
	{
		SUBSTRINGTEST(Right, TEXT("6789"), Right, 4);
		SUBSTRINGTEST(ExactLengthRight, *TestString, Right, 10);
		SUBSTRINGTEST(LongerThanRight, *TestString, Right, 20);
		SUBSTRINGTEST(ZeroRight, TEXT(""), Right, 0);
		SUBSTRINGTEST(NegativeRight, TEXT(""), Right, -1);
	}
	
	SECTION("RightChop")
	{
		SUBSTRINGTEST(RightChop, TEXT("456789"), RightChop, 4);
		SUBSTRINGTEST(ExactLengthRightChop, TEXT(""), RightChop, 10);
		SUBSTRINGTEST(LongerThanRightChop, TEXT(""), RightChop, 20);
		SUBSTRINGTEST(ZeroRightChop, *TestString, RightChop, 0);
		SUBSTRINGTEST(NegativeRightChop, *TestString, RightChop, -1);
	}

	SECTION("Mid")
	{
		SUBSTRINGTEST(Mid, TEXT("456789"), Mid, 4);
		SUBSTRINGTEST(MidCount, TEXT("4567"), Mid, 4, 4);
		SUBSTRINGTEST(MidCountFullLength, *TestString, Mid, 0, 10);
		SUBSTRINGTEST(MidCountOffEnd, TEXT("89"), Mid, 8, 4);
		SUBSTRINGTEST(MidStartAfterEnd, TEXT(""), Mid, 20);
		SUBSTRINGTEST(MidZeroCount, TEXT(""), Mid, 5, 0);
		SUBSTRINGTEST(MidNegativeCount, TEXT(""), Mid, 5, -1);
		SUBSTRINGTEST(MidNegativeStartNegativeEnd, TEXT(""), Mid, -5, 1);
		SUBSTRINGTEST(MidNegativeStartPositiveEnd, TEXT("012"), Mid, -1, 4);
		SUBSTRINGTEST(MidNegativeStartBeyondEnd, *TestString, Mid, -1, 15);
	}

#undef SUBSTRINGTEST
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::FromStringView", "[Core][String][Smoke]")
{
	SECTION("Verify basic construction and assignment from a string view.")
	{
		const TCHAR* Literal = TEXT("Literal");
		const ANSICHAR* AnsiLiteral = "Literal";
		CAPTURE(Literal,AnsiLiteral);
		CHECK_EQUAL(FString(FStringView(Literal)), Literal);
		CHECK_EQUAL(FString(FAnsiStringView(AnsiLiteral)), Literal);
		CHECK_EQUAL((FString(TEXT("Temp")) = FStringView(Literal)), Literal);

		FStringView EmptyStringView;
		FString EmptyString(EmptyStringView);
		CHECK(EmptyString.IsEmpty());
		CHECK(EmptyString.GetAllocatedSize() == 0);

		EmptyString = TEXT("Temp");
		EmptyString = EmptyStringView;
		CHECK(EmptyString.IsEmpty());
		CHECK(EmptyString.GetAllocatedSize() == 0);
	}

	SECTION("Verify assignment from a view of itself.")
	{
		FString AssignEntireString(TEXT("AssignEntireString"));
		AssignEntireString = FStringView(AssignEntireString);
		CAPTURE(AssignEntireString);
		CHECK_EQUAL(AssignEntireString, TEXT("AssignEntireString"));

		FString AssignStartOfString(TEXT("AssignStartOfString"));
		AssignStartOfString = FStringView(AssignStartOfString).Left(11);
		CAPTURE(AssignStartOfString);
		CHECK_EQUAL(AssignStartOfString, TEXT("AssignStart"));

		FString AssignEndOfString(TEXT("AssignEndOfString"));
		AssignEndOfString = FStringView(AssignEndOfString).Right(11);
		CAPTURE(AssignEndOfString);
		CHECK_EQUAL(AssignEndOfString, TEXT("EndOfString"));

		FString AssignMiddleOfString(TEXT("AssignMiddleOfString"));
		AssignMiddleOfString = FStringView(AssignMiddleOfString).Mid(6, 6);
		CAPTURE(AssignMiddleOfString);
		CHECK_EQUAL(AssignMiddleOfString, TEXT("Middle"));
	}

	SECTION("Verify operators taking string views and character arrays")
	{
		FStringView RhsStringView = FStringView(TEXT("RhsNotSZ"), 3);
		FString MovePlusSVResult = FString(TEXT("Lhs")) + RhsStringView;
		CHECK_EQUAL(MovePlusSVResult, TEXT("LhsRhs"));

		FString CopyLhs(TEXT("Lhs"));
		FString CopyPlusSVResult = CopyLhs + RhsStringView;
		CHECK_EQUAL(CopyPlusSVResult, TEXT("LhsRhs"));

		FString MovePlusTCHARsResult = FString(TEXT("Lhs")) + TEXT("Rhs");
		CHECK_EQUAL(MovePlusTCHARsResult, TEXT("LhsRhs"));

		FString CopyPlusTCHARsResult = CopyLhs + TEXT("Rhs");
		CHECK_EQUAL(CopyPlusTCHARsResult, TEXT("LhsRhs"));

		FStringView LhsStringView = FStringView(TEXT("LhsNotSZ"), 3);
		FString SVPlusMoveResult = LhsStringView + FString(TEXT("Rhs"));
		CHECK_EQUAL(SVPlusMoveResult, TEXT("LhsRhs"));

		FString CopyRhs(TEXT("Rhs"));
		FString SVPlusCopyResult = LhsStringView + CopyRhs;
		CHECK_EQUAL(SVPlusCopyResult, TEXT("LhsRhs"));

		FString TCHARsPlusMoveResult = TEXT("Lhs") + FString(TEXT("Rhs"));
		CHECK_EQUAL(TCHARsPlusMoveResult, TEXT("LhsRhs"));

		FString TCHARsPlusCopyResult = TEXT("Lhs") + CopyRhs;
		CHECK_EQUAL(TCHARsPlusCopyResult, TEXT("LhsRhs"));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::ConstructWithSlack", "[Core][String][Smoke]")
{
	// Note that the total capacity of a string might be greater than the string length + slack + a null terminator due to
	// underlying malloc implementations which is why we poll FMemory to see what size of allocation we should be expecting.

	SECTION("Test creating from a valid string with various valid slack value")
	{
		const TCHAR* TestString = TEXT("FooBar");
		const char* TestAsciiString = "FooBar";
		const int32 LengthOfString = TCString<TCHAR>::Strlen(TestString);
		const int32 ExtraSlack = 32;
		const int32 NumElements = LengthOfString + ExtraSlack + 1;

		const SIZE_T ExpectedCapacity = FMemory::QuantizeSize(NumElements * sizeof(TCHAR));

		FString StringFromTChar(TestString, ExtraSlack);
		CHECK_EQUAL(StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		CHECK_EQUAL(StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	SECTION("Test creating from a valid string with a zero slack value")
	{
		const TCHAR* TestString = TEXT("FooBar");
		const char* TestAsciiString = "FooBar";
		const int32 LengthOfString = TCString<TCHAR>::Strlen(TestString);
		const int32 ExtraSlack = 0;
		const int32 NumElements = LengthOfString + ExtraSlack + 1;

		const SIZE_T ExpectedCapacity = FMemory::QuantizeSize(NumElements * sizeof(TCHAR));

		FString StringFromTChar(TestString, ExtraSlack);
		CHECK_EQUAL(StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		CHECK_EQUAL(StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	SECTION("Test creating from an empty string with a valid slack value")
	{
		const TCHAR* TestString = TEXT("");
		const char* TestAsciiString = "";
		const int32 LengthOfString = TCString<TCHAR>::Strlen(TestString);
		const int32 ExtraSlack = 32;
		const int32 NumElements = LengthOfString + ExtraSlack + 1;

		const SIZE_T ExpectedCapacity = FMemory::QuantizeSize(NumElements * sizeof(TCHAR));

		FString StringFromTChar(TestString, ExtraSlack);
		CHECK_EQUAL(StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		CHECK_EQUAL(StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	SECTION("Test creating from an empty string with a zero slack value")
	{
		const TCHAR* TestString = TEXT("");
		const char* TestAsciiString = "";
		const int32 ExtraSlack = 0;

		const SIZE_T ExpectedCapacity = 0u;

		FString StringFromTChar(TestString, ExtraSlack);
		CHECK_EQUAL(StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		CHECK_EQUAL(StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		CHECK_EQUAL(StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::Equality", "[Core][String][Smoke]")
{
	auto TestSelfEquality = [this](const TCHAR* A)
	{
		CHECK(FString(A) == A);
		CHECK(A == FString(A));
		CHECK(FString(A).Equals(FString(A), ESearchCase::CaseSensitive));
		CHECK(FString(A).Equals(FString(A), ESearchCase::IgnoreCase));

		FString Slacker(A);
		Slacker.Reserve(100);
		CHECK(Slacker == FString(A));
	};

	auto TestPairEquality = [this](const TCHAR* A, const TCHAR* B)
	{
		CHECK_EQUAL((FCString::Strcmp(A, B)  == 0), FString(A).Equals(FString(B), ESearchCase::CaseSensitive));
		CHECK_EQUAL((FCString::Strcmp(B, A)  == 0), FString(B).Equals(FString(A), ESearchCase::CaseSensitive));
		CHECK_EQUAL((FCString::Stricmp(A, B) == 0), FString(A).Equals(FString(B), ESearchCase::IgnoreCase));
		CHECK_EQUAL((FCString::Stricmp(B, A) == 0), FString(B).Equals(FString(A), ESearchCase::IgnoreCase));
	};

	const TCHAR* Pairs[][2] =	{ {TEXT(""),	TEXT(" ")}
								, {TEXT("a"),	TEXT("A")}
								, {TEXT("aa"),	TEXT("aA")}
								, {TEXT("az"),	TEXT("AZ")}
								, {TEXT("@["),	TEXT("@]")} };

	for (const TCHAR** Pair : Pairs)
	{
		TestSelfEquality(Pair[0]);
		TestSelfEquality(Pair[1]);
		TestPairEquality(Pair[0], Pair[1]);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::FString::Path Concat Compound Operator", "[Core][String][Smoke]")
{
	// No need to test a nullptr TCHAR* as an input parameter as this is expected to cause a crash
	// No need to test self assignment, clang will catch that was a compiler error (-Wself-assign-overloaded)

	const TCHAR* Path = TEXT("../Path");
	const TCHAR* PathWithTrailingSlash = TEXT("../Path/");
	const TCHAR* Filename = TEXT("File.txt");
	const TCHAR* FilenameWithLeadingSlash = TEXT("/File.txt");
	const TCHAR* CombinedPath = TEXT("../Path/File.txt");
	const TCHAR* CombinedPathWithDoubleSeparator = TEXT("../Path//File.txt");

	// Existing code supported ansi char so we need to test that to avoid potentially breaking license code
	const ANSICHAR* AnsiFilename = "File.txt";
	const ANSICHAR* AnsiFilenameWithLeadingSlash = "/File.txt";

	// The TStringBuilders must be created up front as no easy constructor
	TStringBuilder<128> EmptyStringBuilder;
	TStringBuilder<128> FilenameStringBuilder; FilenameStringBuilder << Filename;
	TStringBuilder<128> FilenameWithLeadingSlashStringBuilder; FilenameWithLeadingSlashStringBuilder << FilenameWithLeadingSlash;
	
#define TEST_EMPTYPATH_EMPTYFILE(Type, Input)						{ FString EmptyPathString; EmptyPathString /= Input; TEST_TRUE(TEXT(Type ": EmptyPath/EmptyFilename to be empty"), EmptyPathString.IsEmpty()); }
#define TEST_VALIDPATH_EMPTYFILE(Type, Input)						{ FString Result(Path); Result /= Input; TEST_EQUAL(TEXT(Type ": ValidPath/EmptyFilename result to be"), Result, PathWithTrailingSlash); } \
																	{ FString Result(PathWithTrailingSlash); Result /= Input; TEST_EQUAL(TEXT(Type " (with extra /): ValidPath/EmptyFilename result to be"), Result, PathWithTrailingSlash); }	
#define TEST_EMPTYPATH_VALIDFILE(Type, Input)						{ FString Result; Result /= Input; TEST_EQUAL(TEXT(Type ": EmptyPath/ValidFilename"), Result, Filename); }
#define TEST_VALIDPATH_VALIDFILE(Type, Path, File)					{ FString Result(Path); Result /= File; TEST_EQUAL(TEXT(Type ": ValidPath/ValidFilename"), Result, CombinedPath); }
#define TEST_VALIDPATH_VALIDFILE_DOUBLE_SEPARATOR(Type, Path, File)	{ FString Result(Path); Result /= File; TEST_EQUAL(TEXT(Type ": ValidPath//ValidFilename"), Result, CombinedPathWithDoubleSeparator); }

	SECTION("Test empty path /= empty file")
	{
		TEST_EMPTYPATH_EMPTYFILE("NullString", FString());
		TEST_EMPTYPATH_EMPTYFILE("EmptyString", FString(TEXT("")));
		TEST_EMPTYPATH_EMPTYFILE("EmptyAnsiLiteralString", "");
		TEST_EMPTYPATH_EMPTYFILE("EmptyLiteralString", TEXT(""));
		TEST_EMPTYPATH_EMPTYFILE("NullStringView", FStringView());
		TEST_EMPTYPATH_EMPTYFILE("EmptyStringView", FStringView(TEXT("")));
		TEST_EMPTYPATH_EMPTYFILE("EmptyStringBuilder", EmptyStringBuilder);
	}
	
	SECTION("Test valid path /= empty file")
	{
		TEST_VALIDPATH_EMPTYFILE("NullString", FString());
		TEST_VALIDPATH_EMPTYFILE("EmptyString", FString(TEXT("")));
		TEST_VALIDPATH_EMPTYFILE("EmptyAnsiLiteralString", "");
		TEST_VALIDPATH_EMPTYFILE("EmptyLiteralString", TEXT(""));
		TEST_VALIDPATH_EMPTYFILE("NullStringView", FStringView());
		TEST_VALIDPATH_EMPTYFILE("EmptyStringView", FStringView(TEXT("")));
		TEST_VALIDPATH_EMPTYFILE("EmptyStringBuilder", EmptyStringBuilder);
	}
	
	SECTION("Test empty path /= valid file")
	{
		TEST_EMPTYPATH_VALIDFILE("String", FString(Filename));
		TEST_EMPTYPATH_VALIDFILE("LiteralString", Filename);
		TEST_EMPTYPATH_VALIDFILE("LiteralAnsiString", AnsiFilename);
		TEST_EMPTYPATH_VALIDFILE("StringView", FStringView(Filename));
	}
	
	SECTION("// Test valid path /= valid file")
	{
		TEST_VALIDPATH_VALIDFILE("String", Path, FString(Filename));
		TEST_VALIDPATH_VALIDFILE("LiteralString", Path, Filename);
		TEST_VALIDPATH_VALIDFILE("LiteralAnsiString", Path, AnsiFilename);
		TEST_VALIDPATH_VALIDFILE("StringView", Path, FStringView(Filename));
		TEST_VALIDPATH_VALIDFILE("StringBuilder", Path, FilenameStringBuilder);
	}
	
	SECTION("Test valid path(ending in / ) /= valid file")
	{
		TEST_VALIDPATH_VALIDFILE("String (path with extra /)", PathWithTrailingSlash, FString(Filename));
		TEST_VALIDPATH_VALIDFILE("LiteralString (path with extra /)", PathWithTrailingSlash, Filename);
		TEST_VALIDPATH_VALIDFILE("LiteralAnsiString (path with extra /)", PathWithTrailingSlash, AnsiFilename);
		TEST_VALIDPATH_VALIDFILE("StringView (path with extra /)", PathWithTrailingSlash, FStringView(Filename));
		TEST_VALIDPATH_VALIDFILE("StringBuilder (path with extra /)", PathWithTrailingSlash, FilenameStringBuilder);
	}
	
	SECTION("Test valid path / valid path + file(starting with / )")
	{
		TEST_VALIDPATH_VALIDFILE("String (filename with extra /)", Path, FString(FilenameWithLeadingSlash));
		TEST_VALIDPATH_VALIDFILE("LiteralString (filename with extra /)", Path, FilenameWithLeadingSlash);
		TEST_VALIDPATH_VALIDFILE("LiteralAnsiString (filename with extra /)", Path, AnsiFilenameWithLeadingSlash);
		TEST_VALIDPATH_VALIDFILE("StringView (filename with extra /)", Path, FStringView(FilenameWithLeadingSlash));
		TEST_VALIDPATH_VALIDFILE("StringBuilder (filename with extra /)", Path, FilenameWithLeadingSlashStringBuilder);
	}
	
	// Appending a file name that starts with a / to a directory that ends with a / will not remove the erroneous / and so 
	// will end up with // in the path, these tests are to show this behavior
	// For example "path/" /= "/file.txt" will result in "path//file.txt" not "path/file.txt"
	TEST_VALIDPATH_VALIDFILE_DOUBLE_SEPARATOR("String (path and filename with extra /)", PathWithTrailingSlash, FString(FilenameWithLeadingSlash));
	TEST_VALIDPATH_VALIDFILE_DOUBLE_SEPARATOR("LiteralString (path and filename with extra /)", PathWithTrailingSlash, FilenameWithLeadingSlash);
	TEST_VALIDPATH_VALIDFILE_DOUBLE_SEPARATOR("LiteralAnsiString (path and filename with extra /)", PathWithTrailingSlash, AnsiFilenameWithLeadingSlash);
	TEST_VALIDPATH_VALIDFILE_DOUBLE_SEPARATOR("StringView (path and filename with extra /)", PathWithTrailingSlash, FStringView(FilenameWithLeadingSlash));
	TEST_VALIDPATH_VALIDFILE_DOUBLE_SEPARATOR("StringBuilder (path and filename with extra /)", PathWithTrailingSlash, FilenameWithLeadingSlashStringBuilder);

#undef TEST_EMPTYPATH_EMPTYFILE
#undef TEST_VALIDPATH_EMPTYFILE
#undef TEST_VALIDPATH_EMPTYFILE
#undef TEST_VALIDPATH_VALIDFILE
}

