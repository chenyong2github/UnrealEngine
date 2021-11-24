// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/StringBuilder.h"
#include "Misc/AutomationTest.h"
#include "Containers/UnrealString.h"
#include "String/Find.h"

static_assert(TIsSame<typename FStringBuilderBase::ElementType, TCHAR>::Value, "FStringBuilderBase must use TCHAR.");
static_assert(TIsSame<typename FAnsiStringBuilderBase::ElementType, ANSICHAR>::Value, "FAnsiStringBuilderBase must use ANSICHAR.");
static_assert(TIsSame<typename FWideStringBuilderBase::ElementType, WIDECHAR>::Value, "FWideStringBuilderBase must use WIDECHAR.");

static_assert(TIsSame<FStringBuilderBase, TStringBuilderBase<TCHAR>>::Value, "FStringBuilderBase must be the same as TStringBuilderBase<TCHAR>.");
static_assert(TIsSame<FAnsiStringBuilderBase, TStringBuilderBase<ANSICHAR>>::Value, "FAnsiStringBuilderBase must be the same as TStringBuilderBase<ANSICHAR>.");
static_assert(TIsSame<FWideStringBuilderBase, TStringBuilderBase<WIDECHAR>>::Value, "FWideStringBuilderBase must be the same as TStringBuilderBase<WIDECHAR>.");

static_assert(TIsContiguousContainer<FStringBuilderBase>::Value, "FStringBuilderBase must be a contiguous container.");
static_assert(TIsContiguousContainer<FAnsiStringBuilderBase>::Value, "FAnsiStringBuilderBase must be a contiguous container.");
static_assert(TIsContiguousContainer<FWideStringBuilderBase>::Value, "FWideStringBuilderBase must be a contiguous container.");

static_assert(TIsContiguousContainer<TStringBuilder<128>>::Value, "TStringBuilder<N> must be a contiguous container.");
static_assert(TIsContiguousContainer<TAnsiStringBuilder<128>>::Value, "TAnsiStringBuilder<N> must be a contiguous container.");
static_assert(TIsContiguousContainer<TWideStringBuilder<128>>::Value, "TWideStringBuilder<N> must be a contiguous container.");

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringBuilderTestAppendString, "System.Core.StringBuilder.AppendString", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringBuilderTestAppendString::RunTest(const FString& Parameters)
{
	// Empty Base
	{
		FStringBuilderBase Builder;
		TestEqual(TEXT("Empty StringBuilderBase Len"), Builder.Len(), 0);
		TestEqual(TEXT("Empty StringBuilderBase ToString"), Builder.ToString(), TEXT(""));
		Builder << TEXT('A');
		TestEqual(TEXT("Append Char to StringBuilderBase"), Builder.ToString(), TEXT("A"));
	}

	// Empty With Buffer
	{
		TStringBuilder<1024> Builder;
		TestEqual(TEXT("Empty StringBuilderWithBuffer Len"), Builder.Len(), 0);
		TestEqual(TEXT("Empty StringBuilderWithBuffer ToString"), Builder.ToString(), TEXT(""));
	}

	// Append Char
	{
		TStringBuilder<7> Builder;
		Builder << TEXT('A') << TEXT('B') << TEXT('C');
		Builder << 'D' << 'E' << 'F';
		TestEqual(TEXT("Append Char"), FStringView(Builder), TEXTVIEW("ABCDEF"));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << 'A' << 'B' << 'C';
		TestEqual(TEXT("Append AnsiChar"), FAnsiStringView(AnsiBuilder), ANSITEXTVIEW("ABC"));
	}

	// Append C String
	{
		TStringBuilder<7> Builder;
		Builder << TEXT("ABC");
		Builder << "DEF";
		TestEqual(TEXT("Append C String"), FStringView(Builder), TEXTVIEW("ABCDEF"));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << "ABC";
		TestEqual(TEXT("Append Ansi C String"), FAnsiStringView(AnsiBuilder), ANSITEXTVIEW("ABC"));
	}

	// Append FStringView
	{
		TStringBuilder<7> Builder;
		Builder << TEXTVIEW("ABC");
		Builder << "DEF"_ASV;
		TestEqual(TEXT("Append FStringView"), FStringView(Builder), TEXTVIEW("ABCDEF"));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << "ABC"_ASV;
		TestEqual(TEXT("Append FAnsiStringView"), FAnsiStringView(AnsiBuilder), "ABC"_ASV);
	}

	// Append FStringBuilderBase
	{
		TStringBuilder<4> Builder;
		Builder << TEXT("ABC");
		TStringBuilder<4> BuilderCopy;
		BuilderCopy << Builder;
		TestEqual(TEXT("Append FStringBuilderBase"), FStringView(BuilderCopy), TEXTVIEW("ABC"));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << "ABC";
		TAnsiStringBuilder<4> AnsiBuilderCopy;
		AnsiBuilderCopy << AnsiBuilder;
		TestEqual(TEXT("Append FAnsiStringBuilderBase"), FAnsiStringView(AnsiBuilderCopy), "ABC"_ASV);
	}

	// Append FString
	{
		TStringBuilder<4> Builder;
		Builder << FString(TEXT("ABC"));
		TestEqual(TEXT("Append FString"), FStringView(Builder), TEXTVIEW("ABC"));
	}

	// Append Char Array
	{
		const auto& String = TEXT("ABC");
		TStringBuilder<4> Builder;
		Builder << String;
		TestEqual(TEXT("Append Char Array"), FStringView(Builder), TEXTVIEW("ABC"));

		const ANSICHAR AnsiString[16] = "ABC";
		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << AnsiString;
		TestEqual(TEXT("Append Char Array"), FAnsiStringView(AnsiBuilder), "ABC"_ASV);
	}


	// Simple ReplaceAt
	{
		TAnsiStringBuilder<4> Builder;
		Builder.ReplaceAt(0, 0, FAnsiStringView());
		TestEqual(TEXT("Replace nothing empty"), Builder.ToString(), "");

		Builder << 'a';
		
		Builder.ReplaceAt(0, 0, FAnsiStringView());
		TestEqual(TEXT("Replace nothing non-empty"), Builder.ToString(), "a");

		Builder.ReplaceAt(0, 1, FAnsiStringView("b"));
		TestEqual(TEXT("Replace single char"), Builder.ToString(), "b");
	}

	// Advanced ReplaceAt
	auto TestReplace = [&](FAnsiStringView Original, FAnsiStringView SearchFor, FAnsiStringView ReplaceWith, FAnsiStringView Expected)
	{
		int32 ReplacePos = UE::String::FindFirst(Original, SearchFor);
		check(ReplacePos != INDEX_NONE);

		TAnsiStringBuilder<4> Builder;
		Builder << Original;
		Builder.ReplaceAt(ReplacePos, SearchFor.Len(), ReplaceWith);

		TestEqual(TEXT("Replace"), FAnsiStringView(Builder), Expected);
	};

	// Test single character erase
	TestReplace(".foo", ".", "", "foo");
	TestReplace("f.oo", ".", "", "foo");
	TestReplace("foo.", ".", "", "foo");
		
	// Test multi character erase
	TestReplace("FooBar", "Bar", "", "Foo");
	TestReplace("FooBar", "Foo", "", "Bar");
	TestReplace("FooBar", "Foo", "fOOO", "fOOOBar");

	// Test replace everything
	TestReplace("Foo", "Foo", "", "");
	TestReplace("Foo", "Foo", "Bar", "Bar");
	TestReplace("Foo", "Foo", "0123456789", "0123456789");

	// Test expanding replace
	TestReplace(".foo", ".", "<dot>", "<dot>foo");
	TestReplace("foo.", ".", "<dot>", "foo<dot>");
	TestReplace("f.oo", ".", "<dot>", "f<dot>oo");

	// Test shrinking replace
	TestReplace("aabbcc", "aa", "A", "Abbcc");
	TestReplace("aabbcc", "bb", "B", "aaBcc");
	TestReplace("aabbcc", "cc", "C", "aabbC");

	// Prepend
	{
		TAnsiStringBuilder<4> Builder;
		Builder.Prepend("");
		TestEqual(TEXT("Prepend nothing to empty"), Builder.Len(), 0);
		
		Builder.Prepend("e");
		TestEqual(TEXT("Prepend single characer"), Builder.ToString(), "e");
		
		Builder.Prepend("abcd");
		TestEqual(TEXT("Prepend substring"), Builder.ToString(), "abcde");

		Builder.Prepend("");
		TestEqual(TEXT("Prepend nothing to non-empty"), Builder.ToString(), "abcde");
	}
	
	// InsertAt
	{
		TAnsiStringBuilder<4> Builder;
		Builder.InsertAt(0, "");
		TestEqual(TEXT("Insert nothing to empty"), Builder.Len(), 0);
		
		Builder.InsertAt(0, "d");
		TestEqual(TEXT("Insert first char"), Builder.ToString(), "d");
		
		Builder.InsertAt(0, "c");
		Builder.InsertAt(0, "a");
		Builder.InsertAt(1, "b");
		Builder.InsertAt(4, "e");
		TestEqual(TEXT("Insert single char"), Builder.ToString(), "abcde");
		
		Builder.InsertAt(3, "__");
		Builder.InsertAt(0, "__");
		Builder.InsertAt(Builder.Len(), "__");
		TestEqual(TEXT("Insert substrings"), Builder.ToString(), "__abc__de__");

		Builder.InsertAt(Builder.Len(), "");
		TestEqual(TEXT("Insert nothing"), Builder.ToString(), "__abc__de__");
	}

	// RemoveAt
	{
		TAnsiStringBuilder<4> Builder;
		Builder << "0123456789";
		Builder.RemoveAt(0, 0);
		Builder.RemoveAt(Builder.Len(), 0);
		Builder.RemoveAt(Builder.Len() / 2, 0);
		TestEqual(TEXT("Remove nothing"), Builder.ToString(), "0123456789");
		
		Builder.RemoveAt(Builder.Len() - 1, 1);
		TestEqual(TEXT("Remove last char"), Builder.ToString(), "012345678");
		
		Builder.RemoveAt(0, 1);
		TestEqual(TEXT("Remove first char"), Builder.ToString(), "12345678");
		
		Builder.RemoveAt(4, 2);
		TestEqual(TEXT("Remove middle"), Builder.ToString(), "123478");

		Builder.RemoveAt(4, 2);
		TestEqual(TEXT("Remove end"), Builder.ToString(), "1234");
		
		Builder.RemoveAt(0, 2);
		TestEqual(TEXT("Remove start"), Builder.ToString(), "34");

		Builder.RemoveAt(0, 2);
		TestEqual(TEXT("Remove start"), Builder.ToString(), "");
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
