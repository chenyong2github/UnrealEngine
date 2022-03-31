// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"
#include "Misc/StringBuilder.h"
#include "TestHarness.h"

static_assert(TIsSame<typename FStringView::ElementType, TCHAR>::Value, "FStringView must use TCHAR.");
static_assert(TIsSame<typename FAnsiStringView::ElementType, ANSICHAR>::Value, "FAnsiStringView must use ANSICHAR.");
static_assert(TIsSame<typename FWideStringView::ElementType, WIDECHAR>::Value, "FWideStringView must use WIDECHAR.");

static_assert(TIsSame<FStringView, TStringView<TCHAR>>::Value, "FStringView must be the same as TStringView<TCHAR>.");
static_assert(TIsSame<FAnsiStringView, TStringView<ANSICHAR>>::Value, "FAnsiStringView must be the same as TStringView<ANSICHAR>.");
static_assert(TIsSame<FWideStringView, TStringView<WIDECHAR>>::Value, "FWideStringView must be the same as TStringView<WIDECHAR>.");

static_assert(TIsContiguousContainer<FStringView>::Value, "FStringView must be a contiguous container.");
static_assert(TIsContiguousContainer<FAnsiStringView>::Value, "FAnsiStringView must be a contiguous container.");
static_assert(TIsContiguousContainer<FWideStringView>::Value, "FWideStringView must be a contiguous container.");

namespace UE::String::Private::TestArgumentDependentLookup
{

struct FTestType
{
	using ElementType = TCHAR;
};

const TCHAR* GetData(const FTestType&) { return TEXT("ABC"); }
int32 GetNum(const FTestType&) { return 3; }
} // UE::String::Private::TestArgumentDependentLookup

template <> struct TIsContiguousContainer<UE::String::Private::TestArgumentDependentLookup::FTestType> { static constexpr bool Value = true; };

constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter;

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Constructor", "[Core][Containers][Smoke]")
{
	// Default View
	{
		FStringView View;
		TEST_EQUAL(TEXT(""), View.Len(), 0);
		TEST_TRUE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Empty View
	{
		FStringView View(TEXT(""));
		TEST_EQUAL(TEXT(""), View.Len(), 0);
		TEST_TRUE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Constructing from nullptr is supported; nullptr interpreted as empty string
	{
		FStringView View(nullptr);
		TEST_EQUAL(TEXT(""), View.Len(), 0);
		TEST_TRUE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Create from a wchar literal
	{
		FStringView View(TEXT("Test Ctor"));
		TEST_EQUAL(TEXT("View length"), View.Len(), FCStringWide::Strlen(TEXT("Test Ctor")));
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringWide::Strncmp(View.GetData(), TEXT("Test Ctor"), View.Len()), 0);
		TEST_FALSE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Create from a sub section of a wchar literal
	{
		FStringView View(TEXT("Test SubSection Ctor"), 4);
		TEST_EQUAL(TEXT("View length"), View.Len(), 4);
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringWide::Strncmp(View.GetData(), TEXT("Test"), View.Len()), 0);
		TEST_FALSE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Create from a FString
	{
		FString String(TEXT("String Object"));
		FStringView View(String);

		TEST_EQUAL(TEXT("View length"), View.Len(), String.Len());
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringWide::Strncmp(View.GetData(), *String, View.Len()), 0);
		TEST_FALSE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Create from an ansi literal
	{
		FAnsiStringView View("Test Ctor");
		TEST_EQUAL(TEXT("View length"), View.Len(), FCStringAnsi::Strlen("Test Ctor"));
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringAnsi::Strncmp(View.GetData(), "Test Ctor", View.Len()), 0);
		TEST_FALSE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Create from a sub section of an ansi literal
	{
		FAnsiStringView View("Test SubSection Ctor", 4);
		TEST_EQUAL(TEXT("View length"), View.Len(), 4);
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringAnsi::Strncmp(View.GetData(), "Test", View.Len()), 0);
		TEST_FALSE(TEXT("View.IsEmpty"), View.IsEmpty());
	}

	// Create using string view literals
	{
		FStringView View = TEXTVIEW("Test");
		FAnsiStringView ViewAnsi = "Test"_ASV;
		FWideStringView ViewWide = WIDETEXT("Test"_WSV);
	}

	// Verify that class template argument deduction is working
	{
		TStringView ViewAnsi((ANSICHAR*)"Test");
		TStringView ViewWide((WIDECHAR*)WIDETEXT("Test"));
		TStringView ViewUtf8((UTF8CHAR*)UTF8TEXT("Test"));
	}
	{
		TStringView ViewAnsi((const ANSICHAR*)"Test");
		TStringView ViewWide((const WIDECHAR*)WIDETEXT("Test"));
		TStringView ViewUtf8((const UTF8CHAR*)UTF8TEXT("Test"));
	}
	{
		TStringView ViewAnsi(WriteToAnsiString<16>("Test"));
		TStringView ViewWide(WriteToWideString<16>(WIDETEXT("Test")));
		TStringView ViewUtf8(WriteToUtf8String<16>(UTF8TEXT("Test")));
	}
	{
		FString String(TEXT("Test"));
		TStringView ViewString(String);
	}

	// Verify that argument-dependent lookup is working for GetData and GetNum
	{
		UE::String::Private::TestArgumentDependentLookup::FTestType Test;
		FStringView View(Test);
		TEST_TRUE(TEXT("StringView ADL"), View.Equals(TEXTVIEW("ABC")));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Iterator", "[Core][Containers][Smoke]")
{
	// Iterate over a string view
	{
		const TCHAR* StringLiteralSrc = TEXT("Iterator!");
		FStringView View(StringLiteralSrc);

		for (TCHAR C : View)
		{
			TEST_TRUE(TEXT("Iterators(0)-Iteration"), C == *StringLiteralSrc++);
		}

		// Make sure we iterated over the entire string
		TEST_TRUE(TEXT("Iterators(0-EndCheck"), *StringLiteralSrc == '\0');
	}

	// Iterate over a partial string view
	{
		const TCHAR* StringLiteralSrc = TEXT("Iterator|with extras!");
		FStringView View(StringLiteralSrc, 8);

		for (TCHAR C : View)
		{
			TEST_TRUE(TEXT("Iterators(1)-Iteration"), C == *StringLiteralSrc++);
		}

		// Make sure we only iterated over the part of the string that the view represents
		TEST_TRUE(TEXT("Iterators(1)-EndCheck"), *StringLiteralSrc == '|');
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Equality Test", "[Core][Containers][Smoke]")
{
	const ANSICHAR* AnsiStringLiteralSrc = "String To Test!";
	const ANSICHAR* AnsiStringLiteralLower = "string to test!";
	const ANSICHAR* AnsiStringLiteralUpper = "STRING TO TEST!";
	const TCHAR* WideStringLiteralSrc = TEXT("String To Test!");
	const TCHAR* WideStringLiteralLower = TEXT("string to test!");
	const TCHAR* WideStringLiteralUpper = TEXT("STRING TO TEST!");
	const TCHAR* WideStringLiteralShort = TEXT("String To");
	const TCHAR* WideStringLiteralLonger = TEXT("String To Test! Extended");

	FStringView WideView(WideStringLiteralSrc);

	TEST_TRUE(TEXT("Equality(0)"), WideView == WideStringLiteralSrc);
	TEST_FALSE(TEXT("Equality(1)"), WideView != WideStringLiteralSrc);
	TEST_TRUE(TEXT("Equality(2)"), WideView == WideStringLiteralLower);
	TEST_FALSE(TEXT("Equality(3)"), WideView != WideStringLiteralLower);
	TEST_TRUE(TEXT("Equality(4)"), WideView == WideStringLiteralUpper);
	TEST_FALSE(TEXT("Equality(5)"), WideView != WideStringLiteralUpper);
	TEST_FALSE(TEXT("Equality(6)"), WideView == WideStringLiteralShort);
	TEST_TRUE(TEXT("Equality(7)"), WideView != WideStringLiteralShort);
	TEST_FALSE(TEXT("Equality(8)"), WideView == WideStringLiteralLonger);
	TEST_TRUE(TEXT("Equality(9)"), WideView != WideStringLiteralLonger);

	TEST_TRUE(TEXT("Equality(10)"), WideStringLiteralSrc == WideView);
	TEST_FALSE(TEXT("Equality(11)"), WideStringLiteralSrc != WideView);
	TEST_TRUE(TEXT("Equality(12)"), WideStringLiteralLower == WideView);
	TEST_FALSE(TEXT("Equality(13)"), WideStringLiteralLower != WideView);
	TEST_TRUE(TEXT("Equality(14)"), WideStringLiteralUpper == WideView);
	TEST_FALSE(TEXT("Equality(15)"), WideStringLiteralUpper != WideView);
	TEST_FALSE(TEXT("Equality(16)"), WideStringLiteralShort == WideView);
	TEST_TRUE(TEXT("Equality(17)"), WideStringLiteralShort != WideView);
	TEST_FALSE(TEXT("Equality(18)"), WideStringLiteralLonger == WideView);
	TEST_TRUE(TEXT("Equality(19)"), WideStringLiteralLonger != WideView);

	FString WideStringSrc = WideStringLiteralSrc;
	FString WideStringLower = WideStringLiteralLower;
	FString WideStringUpper = WideStringLiteralUpper;
	FString WideStringShort = WideStringLiteralShort;
	FString WideStringLonger = WideStringLiteralLonger;

	TEST_TRUE(TEXT("Equality(20)"), WideView == WideStringSrc);
	TEST_FALSE(TEXT("Equality(21)"), WideView != WideStringSrc);
	TEST_TRUE(TEXT("Equality(22)"), WideView == WideStringLower);
	TEST_FALSE(TEXT("Equality(23)"), WideView != WideStringLower);
	TEST_TRUE(TEXT("Equality(24)"), WideView == WideStringUpper);
	TEST_FALSE(TEXT("Equality(25)"), WideView != WideStringUpper);
	TEST_FALSE(TEXT("Equality(26)"), WideView == WideStringShort);
	TEST_TRUE(TEXT("Equality(27)"), WideView != WideStringShort);
	TEST_FALSE(TEXT("Equality(28)"), WideView == WideStringLonger);
	TEST_TRUE(TEXT("Equality(29)"), WideView != WideStringLonger);

	TEST_TRUE(TEXT("Equality(30)"), WideStringSrc == WideView);
	TEST_FALSE(TEXT("Equality(31)"), WideStringSrc != WideView);
	TEST_TRUE(TEXT("Equality(32)"), WideStringLower == WideView);
	TEST_FALSE(TEXT("Equality(33)"), WideStringLower != WideView);
	TEST_TRUE(TEXT("Equality(34)"), WideStringUpper == WideView);
	TEST_FALSE(TEXT("Equality(35)"), WideStringUpper != WideView);
	TEST_FALSE(TEXT("Equality(36)"), WideStringShort == WideView);
	TEST_TRUE(TEXT("Equality(37)"), WideStringShort != WideView);
	TEST_FALSE(TEXT("Equality(38)"), WideStringLonger == WideView);
	TEST_TRUE(TEXT("Equality(39)"), WideStringLonger != WideView);

	FStringView IdenticalView(WideStringLiteralSrc);

	TEST_TRUE(TEXT("Equality(40a)"), WideView == IdenticalView);
	TEST_FALSE(TEXT("Equality(40b)"), WideView != IdenticalView);
	TEST_TRUE(TEXT("Equality(41a)"), IdenticalView == WideView);
	TEST_FALSE(TEXT("Equality(41b)"), IdenticalView != WideView);

	// Views without null termination

	FStringView ShortViewNoNull = WideView.Left(FStringView(WideStringLiteralShort).Len());

	TEST_TRUE(TEXT("Equality(42)"), ShortViewNoNull == WideStringLiteralShort);
	TEST_FALSE(TEXT("Equality(43)"), ShortViewNoNull != WideStringLiteralShort);
	TEST_TRUE(TEXT("Equality(44)"), WideStringLiteralShort == ShortViewNoNull);
	TEST_FALSE(TEXT("Equality(45)"), WideStringLiteralShort != ShortViewNoNull);
	TEST_FALSE(TEXT("Equality(46)"), ShortViewNoNull == WideStringLiteralSrc);
	TEST_TRUE(TEXT("Equality(47)"), ShortViewNoNull != WideStringLiteralSrc);
	TEST_FALSE(TEXT("Equality(48)"), WideStringLiteralSrc == ShortViewNoNull);
	TEST_TRUE(TEXT("Equality(49)"), WideStringLiteralSrc != ShortViewNoNull);

	TEST_TRUE(TEXT("Equality(50)"), ShortViewNoNull == WideStringShort);
	TEST_FALSE(TEXT("Equality(51)"), ShortViewNoNull != WideStringShort);
	TEST_TRUE(TEXT("Equality(52)"), WideStringShort == ShortViewNoNull);
	TEST_FALSE(TEXT("Equality(53)"), WideStringShort != ShortViewNoNull);
	TEST_FALSE(TEXT("Equality(54)"), ShortViewNoNull == WideStringSrc);
	TEST_TRUE(TEXT("Equality(55)"), ShortViewNoNull != WideStringSrc);
	TEST_FALSE(TEXT("Equality(56)"), WideStringSrc == ShortViewNoNull);
	TEST_TRUE(TEXT("Equality(57)"), WideStringSrc != ShortViewNoNull);

	FStringView WideViewNoNull = FStringView(WideStringLiteralLonger).Left(WideView.Len());

	TEST_TRUE(TEXT("Equality(58)"), WideViewNoNull == WideStringLiteralSrc);
	TEST_FALSE(TEXT("Equality(59)"), WideViewNoNull != WideStringLiteralSrc);
	TEST_TRUE(TEXT("Equality(60)"), WideStringLiteralSrc == WideViewNoNull);
	TEST_FALSE(TEXT("Equality(61)"), WideStringLiteralSrc != WideViewNoNull);
	TEST_FALSE(TEXT("Equality(62)"), WideViewNoNull == WideStringLiteralLonger);
	TEST_TRUE(TEXT("Equality(63)"), WideViewNoNull != WideStringLiteralLonger);
	TEST_FALSE(TEXT("Equality(64)"), WideStringLiteralLonger == WideViewNoNull);
	TEST_TRUE(TEXT("Equality(65)"), WideStringLiteralLonger != WideViewNoNull);

	TEST_TRUE(TEXT("Equality(66)"), WideViewNoNull == WideStringLiteralSrc);
	TEST_FALSE(TEXT("Equality(67)"), WideViewNoNull != WideStringLiteralSrc);
	TEST_TRUE(TEXT("Equality(68)"), WideStringLiteralSrc == WideViewNoNull);
	TEST_FALSE(TEXT("Equality(69)"), WideStringLiteralSrc != WideViewNoNull);
	TEST_FALSE(TEXT("Equality(70)"), WideViewNoNull == WideStringLiteralLonger);
	TEST_TRUE(TEXT("Equality(71)"), WideViewNoNull != WideStringLiteralLonger);
	TEST_FALSE(TEXT("Equality(72)"), WideStringLiteralLonger == WideViewNoNull);
	TEST_TRUE(TEXT("Equality(73)"), WideStringLiteralLonger != WideViewNoNull);

	// ANSICHAR / TCHAR

	FAnsiStringView AnsiView(AnsiStringLiteralSrc);
	FAnsiStringView AnsiViewLower(AnsiStringLiteralLower);
	FAnsiStringView AnsiViewUpper(AnsiStringLiteralUpper);

	TEST_TRUE(TEXT("Equality(74)"), AnsiView.Equals(WideView));
	TEST_TRUE(TEXT("Equality(75)"), WideView.Equals(AnsiView));
	TEST_FALSE(TEXT("Equality(76)"), AnsiViewLower.Equals(WideView, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(77)"), AnsiViewLower.Equals(WideView, ESearchCase::IgnoreCase));
	TEST_FALSE(TEXT("Equality(78)"), WideView.Equals(AnsiViewLower, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(79)"), WideView.Equals(AnsiViewLower, ESearchCase::IgnoreCase));
	TEST_FALSE(TEXT("Equality(80)"), AnsiViewUpper.Equals(WideView, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(81)"), AnsiViewUpper.Equals(WideView, ESearchCase::IgnoreCase));
	TEST_FALSE(TEXT("Equality(82)"), WideView.Equals(AnsiViewUpper, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(83)"), WideView.Equals(AnsiViewUpper, ESearchCase::IgnoreCase));

	TEST_TRUE(TEXT("Equality(84)"), WideView.Equals(AnsiStringLiteralSrc));
	TEST_FALSE(TEXT("Equality(85)"), WideView.Equals(AnsiStringLiteralLower, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(86)"), WideView.Equals(AnsiStringLiteralLower, ESearchCase::IgnoreCase));
	TEST_FALSE(TEXT("Equality(87)"), WideView.Equals(AnsiStringLiteralUpper, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(88)"), WideView.Equals(AnsiStringLiteralUpper, ESearchCase::IgnoreCase));
	TEST_TRUE(TEXT("Equality(89)"), AnsiView.Equals(WideStringLiteralSrc));
	TEST_FALSE(TEXT("Equality(90)"), AnsiViewLower.Equals(WideStringLiteralSrc, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(91)"), AnsiViewLower.Equals(WideStringLiteralSrc, ESearchCase::IgnoreCase));
	TEST_FALSE(TEXT("Equality(92)"), AnsiViewUpper.Equals(WideStringLiteralSrc, ESearchCase::CaseSensitive));
	TEST_TRUE(TEXT("Equality(93)"), AnsiViewUpper.Equals(WideStringLiteralSrc, ESearchCase::IgnoreCase));

	// Test equality of empty strings
	{
		const TCHAR* EmptyLiteral = TEXT("");
		const TCHAR* NonEmptyLiteral = TEXT("ABC");
		FStringView EmptyView;
		FStringView NonEmptyView = TEXTVIEW("ABC");
		TEST_TRUE(TEXT("Equals(94)"), EmptyView.Equals(EmptyLiteral));
		TEST_TRUE(TEXT("Equals(95)"), !EmptyView.Equals(NonEmptyLiteral));
		TEST_TRUE(TEXT("Equals(96)"), !NonEmptyView.Equals(EmptyLiteral));
		TEST_TRUE(TEXT("Equals(97)"), EmptyView.Equals(EmptyView));
		TEST_TRUE(TEXT("Equals(98)"), !EmptyView.Equals(NonEmptyView));
		TEST_TRUE(TEXT("Equals(99)"), !NonEmptyView.Equals(EmptyView));
	}

	// Test types convertible to a string view
	static_assert(TIsSame<bool, decltype(FAnsiStringView().Equals(FString()))>::Value, "Error with Equals");
	static_assert(TIsSame<bool, decltype(FWideStringView().Equals(FString()))>::Value, "Error with Equals");
	static_assert(TIsSame<bool, decltype(FAnsiStringView().Equals(TAnsiStringBuilder<16>()))>::Value, "Error with Equals");
	static_assert(TIsSame<bool, decltype(FAnsiStringView().Equals(TWideStringBuilder<16>()))>::Value, "Error with Equals");
	static_assert(TIsSame<bool, decltype(FWideStringView().Equals(TAnsiStringBuilder<16>()))>::Value, "Error with Equals");
	static_assert(TIsSame<bool, decltype(FWideStringView().Equals(TWideStringBuilder<16>()))>::Value, "Error with Equals");
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Comparison Case Sensitive", "[Core][Containers][Smoke]")
{
	// Basic comparisons involving case
	{
		const ANSICHAR* AnsiStringLiteralSrc = "String To Test!";
		const TCHAR* WideStringLiteralSrc = TEXT("String To Test!");
		const TCHAR* WideStringLiteralLower = TEXT("string to test!");
		const TCHAR* WideStringLiteralUpper = TEXT("STRING TO TEST!");

		FStringView WideView(WideStringLiteralSrc);

		TEST_TRUE(TEXT("ComparisonCaseSensitive(0)"), WideView.Compare(WideStringLiteralSrc, ESearchCase::CaseSensitive) == 0);
		TEST_FALSE(TEXT("ComparisonCaseSensitive(1)"), WideView.Compare(WideStringLiteralLower, ESearchCase::CaseSensitive) > 0);
		TEST_FALSE(TEXT("ComparisonCaseSensitive(2)"), WideView.Compare(WideStringLiteralUpper, ESearchCase::CaseSensitive) < 0);

		FStringView EmptyView(TEXT(""));
		TEST_TRUE(TEXT("ComparisonCaseSensitive(3)"), WideView.Compare(EmptyView, ESearchCase::CaseSensitive) > 0);

		FStringView IdenticalView(WideStringLiteralSrc);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(4)"), WideView.Compare(IdenticalView, ESearchCase::CaseSensitive) == 0);

		FAnsiStringView AnsiView(AnsiStringLiteralSrc);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(5)"), WideView.Compare(AnsiView, ESearchCase::CaseSensitive) == 0);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(6)"), WideView.Compare(AnsiStringLiteralSrc, ESearchCase::CaseSensitive) == 0);
	}

	// Test comparisons of different lengths
	{
		const ANSICHAR* AnsiStringLiteralUpper = "ABCDEF";
		const TCHAR* WideStringLiteralUpper = TEXT("ABCDEF");
		const TCHAR* WideStringLiteralLower = TEXT("abcdef");
		const TCHAR* WideStringLiteralLowerShort = TEXT("abc");

		const ANSICHAR* AnsiStringLiteralUpperFirst = "ABCdef";
		const TCHAR* WideStringLiteralUpperFirst = TEXT("ABCdef");
		const TCHAR* WideStringLiteralLowerFirst = TEXT("abcDEF");

		FStringView ViewLongUpper(WideStringLiteralUpper);
		FStringView ViewLongLower(WideStringLiteralLower);

		// Note that the characters after these views are in a different case, this will help catch over read issues
		FStringView ViewShortUpper(WideStringLiteralUpperFirst, 3);
		FStringView ViewShortLower(WideStringLiteralLowerFirst, 3);

		// Same length, different cases
		TEST_TRUE(TEXT("ComparisonCaseSensitive(7)"), ViewLongUpper.Compare(ViewLongLower, ESearchCase::CaseSensitive) < 0);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(8)"), ViewLongLower.Compare(ViewLongUpper, ESearchCase::CaseSensitive) > 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(9)"), ViewLongLower.Compare(AnsiStringLiteralUpper, ESearchCase::CaseSensitive) > 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(10)"), ViewShortUpper.Compare(WideStringLiteralLowerShort, ESearchCase::CaseSensitive) < 0);

		// Same case, different lengths
		TEST_TRUE(TEXT("ComparisonCaseSensitive(11)"), ViewLongUpper.Compare(ViewShortUpper, ESearchCase::CaseSensitive) > 0);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(12)"), ViewShortUpper.Compare(ViewLongUpper, ESearchCase::CaseSensitive) < 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(13)"), ViewShortUpper.Compare(AnsiStringLiteralUpper, ESearchCase::CaseSensitive) < 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(14)"), ViewLongLower.Compare(WideStringLiteralLowerShort, ESearchCase::CaseSensitive) > 0);

		// Different length, different cases
		TEST_TRUE(TEXT("ComparisonCaseSensitive(15)"), ViewLongUpper.Compare(ViewShortLower, ESearchCase::CaseSensitive) < 0);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(16)"), ViewShortLower.Compare(ViewLongUpper, ESearchCase::CaseSensitive) > 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(17)"), ViewShortLower.Compare(AnsiStringLiteralUpper, ESearchCase::CaseSensitive) > 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(18)"), ViewLongUpper.Compare(WideStringLiteralLowerShort, ESearchCase::CaseSensitive) < 0);
	}

	// Test comparisons of empty strings
	{
		const TCHAR* EmptyLiteral = TEXT("");
		const TCHAR* NonEmptyLiteral = TEXT("ABC");
		FStringView EmptyView;
		FStringView NonEmptyView = TEXTVIEW("ABC");
		TEST_TRUE(TEXT("ComparisonEmpty(19)"), EmptyView.Compare(EmptyLiteral) == 0);
		TEST_TRUE(TEXT("ComparisonEmpty(20)"), EmptyView.Compare(NonEmptyLiteral) < 0);
		TEST_TRUE(TEXT("ComparisonEmpty(21)"), NonEmptyView.Compare(EmptyLiteral) > 0);
		TEST_TRUE(TEXT("ComparisonEmpty(22)"), EmptyView.Compare(EmptyView) == 0);
		TEST_TRUE(TEXT("ComparisonEmpty(23)"), EmptyView.Compare(NonEmptyView) < 0);
		TEST_TRUE(TEXT("ComparisonEmpty(24)"), NonEmptyView.Compare(EmptyView) > 0);
	}

	// Test types convertible to a string view
	static_assert(TIsSame<int32, decltype(FAnsiStringView().Compare(FString()))>::Value, "Error with Compare");
	static_assert(TIsSame<int32, decltype(FWideStringView().Compare(FString()))>::Value, "Error with Compare");
	static_assert(TIsSame<int32, decltype(FAnsiStringView().Compare(TAnsiStringBuilder<16>()))>::Value, "Error with Compare");
	static_assert(TIsSame<int32, decltype(FAnsiStringView().Compare(TWideStringBuilder<16>()))>::Value, "Error with Compare");
	static_assert(TIsSame<int32, decltype(FWideStringView().Compare(TAnsiStringBuilder<16>()))>::Value, "Error with Compare");
	static_assert(TIsSame<int32, decltype(FWideStringView().Compare(TWideStringBuilder<16>()))>::Value, "Error with Compare");
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Comparison Case Insensitive", "[Core][Containers][Smoke]")
{
	// Basic comparisons involving case
	{
		const ANSICHAR* AnsiStringLiteralSrc = "String To Test!";
		const TCHAR* WideStringLiteralSrc = TEXT("String To Test!");
		const TCHAR* WideStringLiteralLower = TEXT("string to test!");
		const TCHAR* WideStringLiteralUpper = TEXT("STRING TO TEST!");

		FStringView WideView(WideStringLiteralSrc);

		TEST_TRUE(TEXT("ComparisonCaseInsensitive(0)"), WideView.Compare(WideStringLiteralSrc, ESearchCase::IgnoreCase) == 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(1)"), WideView.Compare(WideStringLiteralLower, ESearchCase::IgnoreCase) == 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(2)"), WideView.Compare(WideStringLiteralUpper, ESearchCase::IgnoreCase) == 0);

		FStringView EmptyView(TEXT(""));
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(3)"), WideView.Compare(EmptyView, ESearchCase::IgnoreCase) > 0);

		FStringView IdenticalView(WideStringLiteralSrc);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(4)"), WideView.Compare(IdenticalView, ESearchCase::IgnoreCase) == 0);

		FAnsiStringView AnsiView(AnsiStringLiteralSrc);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(5)"), WideView.Compare(AnsiView, ESearchCase::IgnoreCase) == 0);
		TEST_TRUE(TEXT("ComparisonCaseSensitive(6)"), WideView.Compare(AnsiStringLiteralSrc, ESearchCase::IgnoreCase) == 0);
	}

	// Test comparisons of different lengths
	{
		const ANSICHAR* AnsiStringLiteralUpper = "ABCDEF";
		const TCHAR* WideStringLiteralUpper = TEXT("ABCDEF");
		const TCHAR* WideStringLiteralLower = TEXT("abcdef");
		const TCHAR* WideStringLiteralLowerShort = TEXT("abc");

		const ANSICHAR* AnsiStringLiteralUpperFirst = "ABCdef";
		const TCHAR* WideStringLiteralUpperFirst = TEXT("ABCdef");
		const TCHAR* WideStringLiteralLowerFirst = TEXT("abcDEF");

		FStringView ViewLongUpper(WideStringLiteralUpper);
		FStringView ViewLongLower(WideStringLiteralLower);

		// Note that the characters after these views are in a different case, this will help catch over read issues
		FStringView ViewShortUpper(WideStringLiteralUpperFirst, 3);
		FStringView ViewShortLower(WideStringLiteralLowerFirst, 3);

		// Same length, different cases
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(7)"), ViewLongUpper.Compare(ViewLongLower, ESearchCase::IgnoreCase) == 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(8)"), ViewLongLower.Compare(ViewLongUpper, ESearchCase::IgnoreCase) == 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(9)"), ViewLongLower.Compare(AnsiStringLiteralUpper, ESearchCase::IgnoreCase) == 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(10)"), ViewShortUpper.Compare(WideStringLiteralLowerShort, ESearchCase::IgnoreCase) == 0);

		// Same case, different lengths
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(11)"), ViewLongUpper.Compare(ViewShortUpper, ESearchCase::IgnoreCase) > 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(12)"), ViewShortUpper.Compare(ViewLongUpper, ESearchCase::IgnoreCase) < 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(13)"), ViewShortUpper.Compare(AnsiStringLiteralUpper, ESearchCase::IgnoreCase) < 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(14)"), ViewLongLower.Compare(WideStringLiteralLowerShort, ESearchCase::IgnoreCase) > 0);

		// Different length, different cases
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(15)"), ViewLongUpper.Compare(ViewShortLower, ESearchCase::IgnoreCase) > 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(16)"), ViewShortLower.Compare(ViewLongUpper, ESearchCase::IgnoreCase) < 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(17)"), ViewShortLower.Compare(AnsiStringLiteralUpper, ESearchCase::IgnoreCase) < 0);
		TEST_TRUE(TEXT("ComparisonCaseInsensitive(18)"), ViewLongUpper.Compare(WideStringLiteralLowerShort, ESearchCase::IgnoreCase) > 0);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::ArrayAccessor", "[Core][Containers][Smoke]")
{
	const TCHAR* SrcString = TEXT("String To Test");
	FStringView View(SrcString);

	for (int32 i = 0; i < View.Len(); ++i)
	{
		TEST_EQUAL(TEXT("the character accessed"), View[i], SrcString[i]);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::ArrayModifiers", "[Core][Containers][Smoke]")
{
	const TCHAR* FullText = TEXT("PrefixSuffix");
	const TCHAR* Prefix = TEXT("Prefix");
	const TCHAR* Suffix = TEXT("Suffix");

	// Remove prefix
	{
		FStringView View(FullText);
		View.RemovePrefix(FCStringWide::Strlen(Prefix));

		TEST_EQUAL(TEXT("View length"), View.Len(), FCStringWide::Strlen(Suffix));
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringWide::Strncmp(View.GetData(), Suffix, View.Len()), 0);
	}

	// Remove suffix
	{
		FStringView View(FullText);
		View.RemoveSuffix(FCStringWide::Strlen(Suffix));

		TEST_EQUAL(TEXT("View length"), View.Len(), FCStringWide::Strlen(Prefix));
		TEST_EQUAL(TEXT("The result of Strncmp"), FCStringWide::Strncmp(View.GetData(), Prefix, View.Len()), 0);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::StartsWith", "[Core][Containers][Smoke]")
{
	// Test an empty view
	{
		FStringView View;
		TEST_TRUE(TEXT(" View.StartsWith"), View.StartsWith(TEXT("")));
		TEST_FALSE(TEXT(" View.StartsWith"), View.StartsWith(TEXT("Text")));
		TEST_FALSE(TEXT(" View.StartsWith"), View.StartsWith(TEXT('A')));
	}

	// Test a valid view with the correct text
	{
		FStringView View(TEXT("String to test"));
		TEST_TRUE(TEXT(" View.StartsWith"), View.StartsWith(TEXT("String")));
		TEST_TRUE(TEXT(" View.StartsWith"), View.StartsWith(TEXT('S')));
	}

	// Test a valid view with incorrect text
	{
		FStringView View(TEXT("String to test"));
		TEST_FALSE(TEXT(" View.StartsWith"), View.StartsWith(TEXT("test")));
		TEST_FALSE(TEXT(" View.StartsWith"), View.StartsWith(TEXT('t')));
	}

	// Test a valid view with the correct text but with different case
	{
		FStringView View(TEXT("String to test"));
		TEST_TRUE(TEXT(" View.StartsWith"), View.StartsWith(TEXT("sTrInG")));

		// Searching by char is case sensitive to keep compatibility with FString
		TEST_FALSE(TEXT(" View.StartsWith"), View.StartsWith(TEXT('s')));
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::EndsWith", "[Core][Containers][Smoke]")
{
	// Test an empty view
	{
		FStringView View;
		TEST_TRUE(TEXT(" View.EndsWith"), View.EndsWith(TEXT("")));
		TEST_FALSE(TEXT(" View.EndsWith"), View.EndsWith(TEXT("Text")));
		TEST_FALSE(TEXT(" View.EndsWith"), View.EndsWith(TEXT('A')));
	}

	// Test a valid view with the correct text
	{
		FStringView View(TEXT("String to test"));
		TEST_TRUE(TEXT(" View.EndsWith"), View.EndsWith(TEXT("test")));
		TEST_TRUE(TEXT(" View.EndsWith"), View.EndsWith(TEXT('t')));
	}

	// Test a valid view with incorrect text
	{
		FStringView View(TEXT("String to test"));
		TEST_FALSE(TEXT(" View.EndsWith"), View.EndsWith(TEXT("String")));
		TEST_FALSE(TEXT(" View.EndsWith"), View.EndsWith(TEXT('S')));
	}

	// Test a valid view with the correct text but with different case
	{
		FStringView View(TEXT("String to test"));
		TEST_TRUE(TEXT(" View.EndsWith"), View.EndsWith(TEXT("TeST")));

		// Searching by char is case sensitive to keep compatibility with FString
		TEST_FALSE(TEXT(" View.EndsWith"), View.EndsWith(TEXT('T'))); 
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::SubStr", "[Core][Containers][Smoke]")
{
	{
		FStringView EmptyView;
		FStringView EmptyResult = EmptyView.SubStr(0, 10);
		TEST_TRUE(TEXT("FStringView::SubStr(0)"), EmptyResult.IsEmpty());

		// The following line is commented out as it would fail an assert and currently we cannot test for this in unit tests 
		// FStringView OutofBoundsResult = EmptyView.SubStr(1000, 10000); 
		FStringView OutofBoundsResult = EmptyView.SubStr(0, 10000);
		TEST_TRUE(TEXT("FStringView::SubStr(1)"), OutofBoundsResult.IsEmpty());
	}

	{
		FStringView View(TEXT("A test string|"));
		FStringView Word0 = View.SubStr(0, 1);
		FStringView Word1 = View.SubStr(2, 4);
		FStringView Word2 = View.SubStr(7, 6);
		FStringView NullTerminatorResult = View.SubStr(14, 1024);	// We can create a substr that starts at the end of the 
																	// string since the null terminator is still valid
		FStringView OutofBoundsResult = View.SubStr(0, 1024);

		TEST_TRUE(TEXT("FStringView::SubStr(2)"), FCString::Strncmp(Word0.GetData(), TEXT("A"), Word0.Len()) == 0);
		TEST_TRUE(TEXT("FStringView::SubStr(3)"), FCString::Strncmp(Word1.GetData(), TEXT("test"), Word1.Len()) == 0);
		TEST_TRUE(TEXT("FStringView::SubStr(4)"), FCString::Strncmp(Word2.GetData(), TEXT("string"), Word2.Len()) == 0);
		TEST_TRUE(TEXT("FStringView::SubStr(5)"), NullTerminatorResult.IsEmpty());
		TEST_TRUE(TEXT("FStringView::SubStr(6)"), View == OutofBoundsResult);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Left", "[Core][Containers][Smoke]")
{
	{
		FStringView EmptyView;
		FStringView EmptyResult = EmptyView.Left(0);
		TEST_TRUE(TEXT("FStringView::Left"), EmptyResult.IsEmpty());

		FStringView OutofBoundsResult = EmptyView.Left(1024);
		TEST_TRUE(TEXT("FStringView::Left"), OutofBoundsResult.IsEmpty());
	}
	
	{
		FStringView View(TEXT("A test string padded"), 13); // "A test string" without null termination
		FStringView Result = View.Left(8);

		TEST_TRUE(TEXT("FStringView::Left"), FCString::Strncmp(Result.GetData(), TEXT("A test s"), Result.Len()) == 0);

		FStringView OutofBoundsResult = View.Left(1024);
		TEST_TRUE(TEXT("FStringView::Left"), FCString::Strncmp(OutofBoundsResult.GetData(), TEXT("A test string"), OutofBoundsResult.Len()) == 0);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::LeftChop", "[Core][Containers][Smoke]")
{
	{
		FStringView EmptyView;
		FStringView EmptyResult = EmptyView.LeftChop(0);
		TEST_TRUE(TEXT("FStringView::LeftChop"), EmptyResult.IsEmpty());

		FStringView OutofBoundsResult = EmptyView.LeftChop(1024);
		TEST_TRUE(TEXT("FStringView::LeftChop"), OutofBoundsResult.IsEmpty());
	}

	{
		FStringView View(TEXT("A test string padded"), 13); // "A test string" without null termination
		FStringView Result = View.LeftChop(5);

		TEST_TRUE(TEXT("FStringView::LeftChop"), FCString::Strncmp(Result.GetData(), TEXT("A test s"), Result.Len()) == 0);

		FStringView OutofBoundsResult = View.LeftChop(1024);
		TEST_TRUE(TEXT("FStringView::LeftChop"), FCString::Strncmp(OutofBoundsResult.GetData(), TEXT("A test string"), OutofBoundsResult.Len()) == 0);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Right", "[Core][Containers][Smoke]")
{
	{
		FStringView EmptyView;
		FStringView EmptyResult = EmptyView.Right(0);
		TEST_TRUE(TEXT("FStringView::Right"), EmptyResult.IsEmpty());

		FStringView OutofBoundsResult = EmptyView.Right(1024);
		TEST_TRUE(TEXT("FStringView::Right"), OutofBoundsResult.IsEmpty());
	}

	{
		FStringView View(TEXT("A test string padded"), 13); // "A test string" without null termination
		FStringView Result = View.Right(8);

		TEST_TRUE(TEXT("FStringView::Right"), FCString::Strncmp(Result.GetData(), TEXT("t string"), Result.Len()) == 0);

		FStringView OutofBoundsResult = View.Right(1024);
		TEST_TRUE(TEXT("FStringView::Right"), FCString::Strncmp(OutofBoundsResult.GetData(), TEXT("A test string"), OutofBoundsResult.Len()) == 0);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::RightChop", "[Core][Containers][Smoke]")
{
	{
		FStringView EmptyView;
		FStringView EmptyResult = EmptyView.RightChop(0);
		TEST_TRUE(TEXT("FStringView::RightChop"), EmptyResult.IsEmpty());

		FStringView OutofBoundsResult = EmptyView.RightChop(1024);
		TEST_TRUE(TEXT("FStringView::RightChop"), OutofBoundsResult.IsEmpty());
	}

	{
		FStringView View(TEXT("A test string padded"), 13); // "A test string" without null termination
		FStringView Result = View.RightChop(3);

		TEST_TRUE(TEXT("FStringView::RightChop"), FCString::Strncmp(Result.GetData(), TEXT("est string"), Result.Len()) == 0);

		FStringView OutofBoundsResult = View.RightChop(1024);
		TEST_TRUE(TEXT("FStringView::RightChop"), FCString::Strncmp(OutofBoundsResult.GetData(), TEXT("A test string"), OutofBoundsResult.Len()) == 0);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Mid", "[Core][Containers][Smoke]")
{
	{
		FStringView EmptyView;
		FStringView EmptyResult = EmptyView.Mid(0, 10);
		TEST_TRUE(TEXT("FStringView::Mid(0)"), EmptyResult.IsEmpty());

		// The following line is commented out as it would fail an assert and currently we cannot test for this in unit tests 
		// FStringView OutofBoundsResult = EmptyView.Mid(1000, 10000); 
		FStringView OutofBoundsResult = EmptyView.Mid(0, 10000);
		TEST_TRUE(TEXT("FStringView::Mid(1)"), OutofBoundsResult.IsEmpty());
	}

	{
		FStringView View(TEXT("A test string|"));
		FStringView Word0 = View.Mid(0, 1);
		FStringView Word1 = View.Mid(2, 4);
		FStringView Word2 = View.Mid(7, 6);
		FStringView NullTerminatorResult = View.Mid(14, 1024);	// We can call Mid with a position that starts at the end of the 
																// string since the null terminator is still valid
		FStringView OutofBoundsResult = View.Mid(0, 1024);

		TEST_TRUE(TEXT("FStringView::Mid(2)"), FCString::Strncmp(Word0.GetData(), TEXT("A"), Word0.Len()) == 0);
		TEST_TRUE(TEXT("FStringView::Mid(3)"), FCString::Strncmp(Word1.GetData(), TEXT("test"), Word1.Len()) == 0);
		TEST_TRUE(TEXT("FStringView::Mid(4)"), FCString::Strncmp(Word2.GetData(), TEXT("string"), Word2.Len()) == 0);
		TEST_TRUE(TEXT("FStringView::Mid(5)"), NullTerminatorResult.IsEmpty());
		TEST_TRUE(TEXT("FStringView::Mid(6)"), View == OutofBoundsResult);
		TEST_TRUE(TEXT("FStringView::Mid(7)"), View.Mid(512, 1024).IsEmpty());
		TEST_TRUE(TEXT("FStringView::Mid(8)"), View.Mid(4, 0).IsEmpty());
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::TrimStartAndEnd", "[Core][Containers][Smoke]")
{
	TEST_TRUE(TEXT("FStringView::TrimStartAndEnd(\"\")"), TEXTVIEW("").TrimStartAndEnd().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimStartAndEnd(\" \")"), TEXTVIEW(" ").TrimStartAndEnd().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimStartAndEnd(\"  \")"), TEXTVIEW("  ").TrimStartAndEnd().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimStartAndEnd(\" \\t\\r\\n\")"), TEXTVIEW(" \t\r\n").TrimStartAndEnd().IsEmpty());

	TEST_EQUAL(TEXT("FStringView::TrimStartAndEnd(\"ABC123\")"), TEXTVIEW("ABC123").TrimStartAndEnd(), TEXTVIEW("ABC123"));
	TEST_EQUAL(TEXT("FStringView::TrimStartAndEnd(\"A \\t\\r\\nB\")"), TEXTVIEW("A \t\r\nB").TrimStartAndEnd(), TEXTVIEW("A \t\r\nB"));
	TEST_EQUAL(TEXT("FStringView::TrimStartAndEnd(\" \\t\\r\\nABC123\\n\\r\\t \")"), TEXTVIEW(" \t\r\nABC123\n\r\t ").TrimStartAndEnd(), TEXTVIEW("ABC123"));
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::TrimStart", "[Core][Containers][Smoke]")
{
	TEST_TRUE(TEXT("FStringView::TrimStart(\"\")"), TEXTVIEW("").TrimStart().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimStart(\" \")"), TEXTVIEW(" ").TrimStart().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimStart(\"  \")"), TEXTVIEW("  ").TrimStart().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimStart(\" \\t\\r\\n\")"), TEXTVIEW(" \t\r\n").TrimStart().IsEmpty());

	TEST_EQUAL(TEXT("FStringView::TrimStart(\"ABC123\")"), TEXTVIEW("ABC123").TrimStart(), TEXTVIEW("ABC123"));
	TEST_EQUAL(TEXT("FStringView::TrimStart(\"A \\t\\r\\nB\")"), TEXTVIEW("A \t\r\nB").TrimStart(), TEXTVIEW("A \t\r\nB"));
	TEST_EQUAL(TEXT("FStringView::TrimStart(\" \\t\\r\\nABC123\\n\\r\\t \")"), TEXTVIEW(" \t\r\nABC123\n\r\t ").TrimStart(), TEXTVIEW("ABC123\n\r\t "));
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::TrimEnd", "[Core][Containers][Smoke]")
{
	TEST_TRUE(TEXT("FStringView::TrimEnd(\"\")"), TEXTVIEW("").TrimEnd().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimEnd(\" \")"), TEXTVIEW(" ").TrimEnd().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimEnd(\"  \")"), TEXTVIEW("  ").TrimEnd().IsEmpty());
	TEST_TRUE(TEXT("FStringView::TrimEnd(\" \\t\\r\\n\")"), TEXTVIEW(" \t\r\n").TrimEnd().IsEmpty());

	TEST_EQUAL(TEXT("FStringView::TrimEnd(\"ABC123\")"), TEXTVIEW("ABC123").TrimEnd(), TEXTVIEW("ABC123"));
	TEST_EQUAL(TEXT("FStringView::TrimEnd(\"A \\t\\r\\nB\")"), TEXTVIEW("A \t\r\nB").TrimEnd(), TEXTVIEW("A \t\r\nB"));
	TEST_EQUAL(TEXT("FStringView::TrimEnd(\" \\t\\r\\nABC123\\n\\r\\t \")"), TEXTVIEW(" \t\r\nABC123\n\r\t ").TrimEnd(), TEXTVIEW(" \t\r\nABC123"));
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::FindChar", "[Core][Containers][Smoke]")
{
	FStringView EmptyView;
	FStringView View = TEXT("aBce Fga");

	{
		int32 Index = INDEX_NONE;
		TEST_FALSE(TEXT("FStringView::FindChar-Return(0)"), EmptyView.FindChar(TEXT('a'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(0)"), Index, INDEX_NONE);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_TRUE(TEXT("FStringView::FindChar-Return(1)"), View.FindChar(TEXT('a'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(1)"), Index, 0);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_TRUE(TEXT("FStringView::FindChar-Return(2)"), View.FindChar(TEXT('F'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(2)"), Index, 5);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_FALSE(TEXT("FStringView::FindChar-Return(3)"), View.FindChar(TEXT('A'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(3)"), Index, INDEX_NONE);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_FALSE(TEXT("FStringView::FindChar-Return(4)"), View.FindChar(TEXT('d'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(4)"), Index, INDEX_NONE);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_TRUE(TEXT("FStringView::FindChar-Return(5)"), View.FindChar(TEXT(' '), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(5)"), Index, 4);
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::FindLastChar", "[Core][Containers][Smoke]")
{
	FStringView EmptyView;
	FStringView View = TEXT("aBce Fga");

	{
		int32 Index = INDEX_NONE;
		TEST_FALSE(TEXT("FStringView::FindChar-Return(0)"), EmptyView.FindLastChar(TEXT('a'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(0)"), Index, INDEX_NONE);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_TRUE(TEXT("FStringView::FindChar-Return(1)"), View.FindLastChar(TEXT('a'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(1)"), Index, 7);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_TRUE(TEXT("FStringView::FindChar-Return(2)"), View.FindLastChar(TEXT('B'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(2)"), Index, 1);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_FALSE(TEXT("FStringView::FindChar-Return(3)"), View.FindLastChar(TEXT('A'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(3)"), Index, INDEX_NONE);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_FALSE(TEXT("FStringView::FindChar-Return(4)"), View.FindLastChar(TEXT('d'), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(4)"), Index, INDEX_NONE);
	}

	{
		int32 Index = INDEX_NONE;
		TEST_TRUE(TEXT("FStringView::FindChar-Return(5)"), View.FindLastChar(TEXT(' '), Index));
		TEST_EQUAL(TEXT("FStringView::FindChar-Index(5)"), Index, 4);
	}
}

void TestSlicing(FAutomationTestFixture& Test, const FString& Str)
{
	const FStringView View = Str;
	const int32       Len  = Str.Len();

	// Left tests
	{
		// Try all lengths of the string, including +/- 5 either side
		for (int32 Index = -5; Index != Len + 5; ++Index)
		{
			FString     Substring = Str .Left(Index);
			FStringView Subview   = View.Left(Index);

			TEST_EQUAL(FString::Printf(TEXT("FStringView(\"%s\")::Left(%d)"), *Str, Index), FString(Subview), Substring);
		}
	}

	// LeftChop tests
	{
		// Try all lengths of the string, including +/- 5 either side
		for (int32 Index = -5; Index != Len + 5; ++Index)
		{
			FString     Substring = Str .LeftChop(Index);
			FStringView Subview   = View.LeftChop(Index);

			TEST_EQUAL(FString::Printf(TEXT("FStringView(\"%s\")::LeftChop(%d)"), *Str, Index), FString(Subview), Substring);
		}
	}

	// Right tests
	{
		// Try all lengths of the string, including +/- 5 either side
		for (int32 Index = -5; Index != Len + 5; ++Index)
		{
			FString     Substring = Str .Right(Index);
			FStringView Subview   = View.Right(Index);

			TEST_EQUAL(FString::Printf(TEXT("FStringView(\"%s\")::Right(%d)"), *Str, Index), FString(Subview), Substring);
		}
	}

	// RightChop tests
	{
		// Try all lengths of the string, including +/- 5 either side
		for (int32 Index = -5; Index != Len + 5; ++Index)
		{
			FString     Substring = Str .RightChop(Index);
			FStringView Subview   = View.RightChop(Index);

			TEST_EQUAL(FString::Printf(TEXT("FStringView(\"%s\").RightChop(%d)"), *Str, Index), FString(Subview), Substring);
		}
	}

	// Mid tests
	{
		// Try all lengths of the string, including +/- 5 either side
		for (int32 Index = -5; Index != Len + 5; ++Index)
		{
			for (int32 Count = -5; Count != Len + 5; ++Count)
			{
				FString     Substring = Str .Mid(Index, Count);
				FStringView Subview   = View.Mid(Index, Count);

				TEST_EQUAL(FString::Printf(TEXT("FStringView(\"%s\")::Mid(%d, %d)"), *Str, Index, Count), FString(Subview), Substring);
			}
		}

		// Test near limits of int32
		for (int32 IndexOffset = 0; IndexOffset != Len + 5; ++IndexOffset)
		{
			for (int32 CountOffset = 0; CountOffset != Len + 5; ++CountOffset)
			{
				int32 Index = MIN_int32 + IndexOffset;
				int32 Count = MAX_int32 - CountOffset;

				FString     Substring = Str .Mid(Index, Count);
				FStringView Subview   = View.Mid(Index, Count);

				TEST_EQUAL(FString::Printf(TEXT("FStringView(\"%s\")::Mid(%d, %d)"), *Str, Index, Count), FString(Subview), Substring);
			}
		}
	}
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Containers::FStringView::Slice", "[Core][Containers][Smoke]")
{
	// We assume that FString has already passed its tests, and we just want views to be consistent with it

	// Test an aribtrary string
	TestSlicing(*this, TEXT("Test string"));

	// Test an empty string
	TestSlicing(*this, FString());

	// Test an null-terminator-only empty string
	FString TerminatorOnly;
	TerminatorOnly.GetCharArray().Add(TEXT('\0'));
	TestSlicing(*this, TerminatorOnly);
}
