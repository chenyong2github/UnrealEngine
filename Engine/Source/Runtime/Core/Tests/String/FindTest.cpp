// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/Find.h"
#include "TestHarness.h"

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindFirst", "[Core][String][Smoke]")
{
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("A")), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("a"), ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("b")), 1);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("B")), 4);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("B"), ESearchCase::IgnoreCase), 1);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("a")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("D"), ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("D")), 11);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("d"), ESearchCase::IgnoreCase), 11);

	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("AbC")), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("ABC")), 3);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("Bc"), ESearchCase::IgnoreCase), 1);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("ab")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("CD"), ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("BD")), 10);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("Bd"), ESearchCase::IgnoreCase), 10);

	CHECK_EQUAL(UE::String::FindFirst(TEXT(""), TEXT("A")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("A"), TEXT("A")), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("A"), TEXT("A"), ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("ABC"), TEXT("ABC")), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("ABC"), TEXT("abc"), ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindFirst(TEXT("AB"), TEXT("ABC")), INDEX_NONE);

	CHECK_EQUAL(UE::String::FindFirst("AbCABCAbCABC", "ABC"), 3);

	CHECK_EQUAL(UE::String::FindFirst(FStringView(nullptr, 0), TEXT("SearchTerm")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(FStringView(), TEXT("SearchTerm")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirst(FString(), TEXT("SearchTerm")), INDEX_NONE);
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindLast", "[Core][String][Smoke]")
{
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("b")), 7);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("B")), 10);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("b"), ESearchCase::IgnoreCase), 10);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("a")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("D"), ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABD"), TEXT("D")), 11);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABD"), TEXT("d"), ESearchCase::IgnoreCase), 11);

	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("AbC")), 6);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("ABC")), 9);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("Bc"), ESearchCase::IgnoreCase), 10);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("ab")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("CD"), ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("BC")), 10);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("Bc"), ESearchCase::IgnoreCase), 10);

	CHECK_EQUAL(UE::String::FindLast(TEXT(""), TEXT("A")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(TEXT("A"), TEXT("A")), 0);
	CHECK_EQUAL(UE::String::FindLast(TEXT("A"), TEXT("A"), ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindLast(TEXT("ABC"), TEXT("ABC")), 0);
	CHECK_EQUAL(UE::String::FindLast(TEXT("ABC"), TEXT("abc"), ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindLast(TEXT("AB"), TEXT("ABC")), INDEX_NONE);

	CHECK_EQUAL(UE::String::FindLast("AbCABCAbCABC", "ABC"), 9);

	CHECK_EQUAL(UE::String::FindLast(FStringView(nullptr, 0), TEXT("SearchTerm")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(FStringView(), TEXT("SearchTerm")), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLast(FString(), TEXT("SearchTerm")), INDEX_NONE);
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindFirstOfAny", "[Core][String][Smoke]")
{
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("A"), TEXT("B")}), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("a"), TEXT("B")}, ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("b")}), 1);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}), 4);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}, ESearchCase::IgnoreCase), 1);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("D"), TEXT("a")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("E"), TEXT("D")}, ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("D")}), 11);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("d")}, ESearchCase::IgnoreCase), 11);

	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("AbC")}), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("CABc"), TEXT("ABC")}), 3);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("ABD"), TEXT("Bc")}, ESearchCase::IgnoreCase), 1);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("bc"), TEXT("ab")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("DA"), TEXT("CD")}, ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABD"), {TEXT("BD"), TEXT("CABB")}), 10);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AbCABCAbCABD"), {TEXT("Bd"), TEXT("CABB")}, ESearchCase::IgnoreCase), 10);

	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT(""), {TEXT("A"), TEXT("B")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}, ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("ABC"), {TEXT("ABC"), TEXT("BC")}), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("ABC"), {TEXT("abc"), TEXT("bc")}, ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindFirstOfAny(TEXT("AB"), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);

	CHECK_EQUAL(UE::String::FindFirstOfAny("AbCABCAbCABC", {"CABc", "ABC"}), 3);

	CHECK_EQUAL(UE::String::FindFirstOfAny(FStringView(nullptr, 0), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(FStringView(), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindFirstOfAny(FString(), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindLastOfAny", "[Core][String][Smoke]")
{
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("a"), TEXT("b")}), 7);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("a"), TEXT("b")}, ESearchCase::IgnoreCase), 10);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("b")}), 7);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}), 10);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}, ESearchCase::IgnoreCase), 11);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("D"), TEXT("a")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("E"), TEXT("D")}, ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("D")}), 11);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("d")}, ESearchCase::IgnoreCase), 11);

	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("AbC")}), 6);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("CABc"), TEXT("ABC")}), 9);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("ABD"), TEXT("Bc")}, ESearchCase::IgnoreCase), 10);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("bc"), TEXT("ab")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("DA"), TEXT("CD")}, ESearchCase::IgnoreCase), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABD"), {TEXT("BD"), TEXT("CABB")}), 10);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AbCABCAbCABD"), {TEXT("Bd"), TEXT("CABB")}, ESearchCase::IgnoreCase), 10);

	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT(""), {TEXT("A"), TEXT("B")}), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}), 0);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}, ESearchCase::IgnoreCase), 0);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("ABC"), {TEXT("ABC"), TEXT("BC")}), 1);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("ABC"), {TEXT("abc"), TEXT("bc")}, ESearchCase::IgnoreCase), 1);
	CHECK_EQUAL(UE::String::FindLastOfAny(TEXT("AB"), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);

	CHECK_EQUAL(UE::String::FindLastOfAny("AbCABCAbCABC", {"CABc", "ABC"}), 9);

	CHECK_EQUAL(UE::String::FindLastOfAny(FStringView(nullptr, 0), { TEXT("ABC"), TEXT("ABD") }), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(FStringView(), { TEXT("ABC"), TEXT("ABD") }), INDEX_NONE);
	CHECK_EQUAL(UE::String::FindLastOfAny(FString(), { TEXT("ABC"), TEXT("ABD") }), INDEX_NONE);
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindFirstChar", "[Core][String][Smoke]")
{
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('b')), 1);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('B')), 4);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('B'), ESearchCase::IgnoreCase), 1);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('a')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('D'), ESearchCase::IgnoreCase), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABD"), TEXT('D')), 11);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABD"), TEXT('d'), ESearchCase::IgnoreCase), 11);

	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT(""), TEXT('A')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("A"), TEXT('A')), 0);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("A"), TEXT('A'), ESearchCase::IgnoreCase), 0);

	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar("AbCABCAbCABC", 'B'), 4);

	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(FStringView(nullptr, 0), TEXT('A')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(FStringView(), TEXT('A')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstChar"), UE::String::FindFirstChar(FString(), TEXT('A')), INDEX_NONE);
}


TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindLastChar", "[Core][String][Smoke]")
{
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('b')), 7);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('B')), 10);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('b'), ESearchCase::IgnoreCase), 10);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('a')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('D'), ESearchCase::IgnoreCase), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABD"), TEXT('D')), 11);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABD"), TEXT('d'), ESearchCase::IgnoreCase), 11);

	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT(""), TEXT('A')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("A"), TEXT('A')), 0);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("A"), TEXT('A'), ESearchCase::IgnoreCase), 0);

	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar("AbCABCAbCABC", 'B'), 10);

	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(FStringView(nullptr, 0), TEXT('A')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(FStringView(), TEXT('A')), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastChar"), UE::String::FindLastChar(FString(), TEXT('A')), INDEX_NONE);
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindFirstOfAnyChar", "[Core][String][Smoke]")
{
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('b')}), 1);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}), 4);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}, ESearchCase::IgnoreCase), 1);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('D'), TEXT('a')}), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('E'), TEXT('D')}, ESearchCase::IgnoreCase), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('D')}), 11);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('d')}, ESearchCase::IgnoreCase), 11);

	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT(""), {TEXT('A'), TEXT('B')}), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}), 0);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}, ESearchCase::IgnoreCase), 0);

	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar("AbCABCAbcABC", {'c', 'B'}), 4);

	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(FStringView(nullptr, 0), { TEXT('A'), TEXT('B') }), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(FStringView(), { TEXT('A'), TEXT('B') }), INDEX_NONE);
	TEST_EQUAL(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(FString(), { TEXT('A'), TEXT('B') }), INDEX_NONE);
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::String::Misc::FindLastOfAnyChar", "[Core][String][Smoke]")
{
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('b')}), 7);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}), 10);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}, ESearchCase::IgnoreCase), 11);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('D'), TEXT('a')}), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('E'), TEXT('D')}, ESearchCase::IgnoreCase), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('D')}), 11);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('d')}, ESearchCase::IgnoreCase), 11);

	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT(""), {TEXT('A'), TEXT('B')}), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}), 0);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}, ESearchCase::IgnoreCase), 0);

	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar("AbCABCAbcABC", {'c', 'B'}), 10);

	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(FStringView(nullptr, 0), { TEXT('A'), TEXT('B') }), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(FStringView(), { TEXT('A'), TEXT('B') }), INDEX_NONE);
	TEST_EQUAL(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(FString(), { TEXT('A'), TEXT('B') }), INDEX_NONE);
}
