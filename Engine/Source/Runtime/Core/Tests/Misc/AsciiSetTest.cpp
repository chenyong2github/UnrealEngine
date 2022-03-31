// Copyright Epic Games, Inc. All Rights Reserved.


#include "Containers/StringView.h"
#include "Misc/AsciiSet.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::Misc::FAsciiSet::AsciiSet", "[Core][Misc][Smoke]")
{
	constexpr FAsciiSet Whitespaces(" \v\f\t\r\n");
	TEST_TRUE(TEXT("Contains"), Whitespaces.Contains(' '));
	TEST_TRUE(TEXT("Contains"), Whitespaces.Contains('\n'));
	TEST_FALSE(TEXT("Contains"), Whitespaces.Contains('a'));
	TEST_FALSE(TEXT("Contains no extended ASCII"), Whitespaces.Contains('\x80'));
	TEST_FALSE(TEXT("Contains no extended ASCII"), Whitespaces.Contains('\xA0'));
	TEST_FALSE(TEXT("Contains no extended ASCII"), Whitespaces.Contains('\xFF'));

	constexpr FAsciiSet Aa("Aa");
	uint32 ANum = 0;
	for (char32_t C = 0; C < 512; ++C)
	{
		ANum += Aa.Contains(C);
	}
	TEST_EQUAL("Contains no wide", ANum, 2u);

	constexpr FAsciiSet NonWhitespaces = ~Whitespaces;
	uint32 WhitespaceNum = 0;
	for (uint8 Char = 0; Char < 128; ++Char)
	{
		WhitespaceNum += !!Whitespaces.Test(Char);
		TEST_EQUAL(TEXT("Inverse"), !!Whitespaces.Test(Char), !NonWhitespaces.Test(Char));
	}
	TEST_EQUAL(TEXT("Num"), WhitespaceNum, 6);

	// TODO [ML]
	//TEST_EQUAL(TEXT("Skip"), FAsciiSet::Skip(TEXT("  \t\tHello world!"), Whitespaces), TEXT("Hello world!"));
	//TEST_EQUAL(TEXT("Skip"), FAsciiSet::Skip(TEXT("Hello world!"), Whitespaces), TEXT("Hello world!"));
	//TEST_EQUAL(TEXT("Skip to extended ASCII"), FAsciiSet::Skip(" " "\xA0" " abc", Whitespaces), "\xA0" " abc");
	//TEST_EQUAL(TEXT("Skip to wide"), FAsciiSet::Skip(TEXT(" 变 abc"), Whitespaces), TEXT("变 abc"));
	TEST_EQUAL(TEXT("AdvanceToFirst"), *FAsciiSet::FindFirstOrEnd("NonWhitespace\t \nNonWhitespace", Whitespaces), '\t');
	TEST_EQUAL(TEXT("AdvanceToLast"), *FAsciiSet::FindLastOrEnd("NonWhitespace\t \nNonWhitespace", Whitespaces), '\n');
	TEST_EQUAL(TEXT("AdvanceToLast"), *FAsciiSet::FindLastOrEnd("NonWhitespace\t NonWhitespace\n", Whitespaces), '\n');
	TEST_EQUAL(TEXT("AdvanceToFirst"), *FAsciiSet::FindFirstOrEnd("NonWhitespaceNonWhitespace", Whitespaces), '\0');
	TEST_EQUAL(TEXT("AdvanceToLast"), *FAsciiSet::FindLastOrEnd("NonWhitespaceNonWhitespace", Whitespaces), '\0');

	constexpr FAsciiSet Lowercase("abcdefghijklmnopqrstuvwxyz");
	TEST_EQUAL(TEXT("TrimPrefixWithout"), FAsciiSet::TrimPrefixWithout("ABcdEF"_ASV, Lowercase), "cdEF"_ASV);
	TEST_EQUAL(TEXT("FindPrefixWithout"), FAsciiSet::FindPrefixWithout("ABcdEF"_ASV, Lowercase), "AB"_ASV);
	TEST_EQUAL(TEXT("TrimSuffixWithout"), FAsciiSet::TrimSuffixWithout("ABcdEF"_ASV, Lowercase), "ABcd"_ASV);
	TEST_EQUAL(TEXT("FindSuffixWithout"), FAsciiSet::FindSuffixWithout("ABcdEF"_ASV, Lowercase), "EF"_ASV);
	TEST_EQUAL(TEXT("TrimPrefixWithout none"), FAsciiSet::TrimPrefixWithout("same"_ASV, Lowercase), "same"_ASV);
	TEST_EQUAL(TEXT("FindPrefixWithout none"), FAsciiSet::FindPrefixWithout("same"_ASV, Lowercase), ""_ASV);
	TEST_EQUAL(TEXT("TrimSuffixWithout none"), FAsciiSet::TrimSuffixWithout("same"_ASV, Lowercase), "same"_ASV);
	TEST_EQUAL(TEXT("FindSuffixWithout none"), FAsciiSet::FindSuffixWithout("same"_ASV, Lowercase), ""_ASV);
	TEST_EQUAL(TEXT("TrimPrefixWithout empty"), FAsciiSet::TrimPrefixWithout(""_ASV, Lowercase), ""_ASV);
	TEST_EQUAL(TEXT("FindPrefixWithout empty"), FAsciiSet::FindPrefixWithout(""_ASV, Lowercase), ""_ASV);
	TEST_EQUAL(TEXT("TrimSuffixWithout empty"), FAsciiSet::TrimSuffixWithout(""_ASV, Lowercase), ""_ASV);
	TEST_EQUAL(TEXT("FindSuffixWithout empty"), FAsciiSet::FindSuffixWithout(""_ASV, Lowercase), ""_ASV);


	auto TestHasFunctions = [&](auto MakeString)
	{
		constexpr FAsciiSet XmlEscapeChars("&<>\"'");
		TEST_TRUE(TEXT("None"), FAsciiSet::HasNone(MakeString("No escape chars"), XmlEscapeChars));
		TEST_FALSE(TEXT("Any"), FAsciiSet::HasAny(MakeString("No escape chars"), XmlEscapeChars));
		TEST_FALSE(TEXT("Only"), FAsciiSet::HasOnly(MakeString("No escape chars"), XmlEscapeChars));

		TEST_TRUE(TEXT("None"), FAsciiSet::HasNone(MakeString(""), XmlEscapeChars));
		TEST_FALSE(TEXT("Any"), FAsciiSet::HasAny(MakeString(""), XmlEscapeChars));
		TEST_TRUE(TEXT("Only"), FAsciiSet::HasOnly(MakeString(""), XmlEscapeChars));

		TEST_FALSE(TEXT("None"), FAsciiSet::HasNone(MakeString("&<>\"'"), XmlEscapeChars));
		TEST_TRUE(TEXT("Any"), FAsciiSet::HasAny(MakeString("&<>\"'"), XmlEscapeChars));
		TEST_TRUE(TEXT("Only"), FAsciiSet::HasOnly(MakeString("&<>\"'"), XmlEscapeChars));

		TEST_FALSE(TEXT("None"), FAsciiSet::HasNone(MakeString("&<>\"' and more"), XmlEscapeChars));
		TEST_TRUE(TEXT("Any"), FAsciiSet::HasAny(MakeString("&<>\"' and more"), XmlEscapeChars));
		TEST_FALSE(TEXT("Only"), FAsciiSet::HasOnly(MakeString("&<>\"' and more"), XmlEscapeChars));
	};
	TestHasFunctions([](const char* Str) { return Str; });
	TestHasFunctions([](const char* Str) { return FAnsiStringView(Str); });
	TestHasFunctions([](const char* Str) { return FString(Str); });


	constexpr FAsciiSet Abc("abc");
	constexpr FAsciiSet Abcd = Abc + 'd';
	TEST_TRUE(TEXT("Add"), Abcd.Contains('a'));
	TEST_TRUE(TEXT("Add"), Abcd.Contains('b'));
	TEST_TRUE(TEXT("Add"), Abcd.Contains('c'));
	TEST_TRUE(TEXT("Add"), Abcd.Contains('d'));
	TEST_FALSE(TEXT("Add"), Abcd.Contains('e'));
}