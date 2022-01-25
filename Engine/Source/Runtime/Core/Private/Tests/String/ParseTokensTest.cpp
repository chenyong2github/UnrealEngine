// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseTokens.h"

#include "Algo/Compare.h"
#include "Misc/AutomationTest.h"
#include "Misc/StringBuilder.h"
#include "Templates/EqualTo.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringParseTokensByStringTest, "System.Core.String.ParseTokens.ByString", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool StringParseTokensByStringTest::RunTest(const FString& Parameters)
{
	using EOptions = UE::String::EParseTokensOptions;

	auto RunParseTokensTest = [this](
		FStringView View,
		std::initializer_list<FStringView> Delimiters,
		std::initializer_list<FStringView> ExpectedTokens,
		EOptions Options = EOptions::None)
	{
		TArray<FStringView, TInlineAllocator<8>> ResultTokens;
		if (GetNum(Delimiters) == 1)
		{
			UE::String::ParseTokens(View, GetData(Delimiters)[0], ResultTokens, Options);
		}
		else
		{
			UE::String::ParseTokensMultiple(View, Delimiters, ResultTokens, Options);
		}
		if (!Algo::Compare(ResultTokens, ExpectedTokens))
		{
			TStringBuilder<512> Error;
			Error << TEXTVIEW("UE::String::ParseTokens[Multiple] failed to parse \"") << View << TEXTVIEW("\" with delimiters {");
			Error.JoinQuoted(Delimiters, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("} result {");
			Error.JoinQuoted(ResultTokens, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("} expected {");
			Error.JoinQuoted(ExpectedTokens, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("}");
			AddError(Error.ToString());
		}
	};

	constexpr EOptions KeepEmpty = EOptions::None;
	constexpr EOptions SkipEmpty = EOptions::SkipEmpty;
	constexpr EOptions IgnoreCase = EOptions::IgnoreCase;
	constexpr EOptions Trim = EOptions::Trim;

	RunParseTokensTest(TEXT(""),         {},                       {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(""),         {},                       {TEXT("")},                         KeepEmpty);
	RunParseTokensTest(TEXT("ABC"),      {},                       {TEXT("ABC")});

	RunParseTokensTest(TEXT(""),         {TEXT(",")},              {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(""),         {TEXT(",")},              {TEXT("")},                         KeepEmpty);
	RunParseTokensTest(TEXT(","),        {TEXT(",")},              {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(","),        {TEXT(",")},              {TEXT(""), TEXT("")},               KeepEmpty);
	RunParseTokensTest(TEXT(",,"),       {TEXT(",")},              {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(",,"),       {TEXT(",")},              {TEXT(""), TEXT(""), TEXT("")},     KeepEmpty);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(",")},              {TEXT(" ")},                        SkipEmpty);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(",")},              {},                                 SkipEmpty | Trim);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(",")},              {TEXT(""), TEXT(" "), TEXT("")},    KeepEmpty);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(",")},              {TEXT(""), TEXT(""), TEXT("")},    KeepEmpty | Trim);
	RunParseTokensTest(TEXT("ABC"),      {TEXT(",")},              {TEXT("ABC")});
	RunParseTokensTest(TEXT("A,,C"),     {TEXT(",")},              {TEXT("A"), TEXT("C")},             SkipEmpty);
	RunParseTokensTest(TEXT("A,,C"),     {TEXT(",")},              {TEXT("A"), TEXT(""), TEXT("C")},   KeepEmpty);
	RunParseTokensTest(TEXT("A,\tB\t,C"), {TEXT(",")},             {TEXT("A"), TEXT("\tB\t"), TEXT("C")});
	RunParseTokensTest(TEXT(",A, B ,C,"), {TEXT(",")},             {TEXT("A"), TEXT(" B "), TEXT("C")},                     SkipEmpty);
	RunParseTokensTest(TEXT(",A, B ,C,"), {TEXT(",")},             {TEXT(""), TEXT("A"), TEXT(" B "), TEXT("C"), TEXT("")}, KeepEmpty);
	RunParseTokensTest(TEXT("A\u2022B\u2022C"), {TEXT("\u2022")},  {TEXT("A"), TEXT("B"), TEXT("C")});

	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("AB")},             {TEXT("CD"), TEXT("CD")},           SkipEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("AB")},             {TEXT(""), TEXT("CD"), TEXT("CD")}, KeepEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("ABCD")},           {},                                 SkipEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("ABCD")},           {TEXT(""), TEXT(""), TEXT("")},     KeepEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("DA")},             {TEXT("ABC"), TEXT("BCD")});

	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("B"),  TEXT("D")},  {TEXT("A"), TEXT("C"), TEXT("A"), TEXT("C")},           SkipEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("B"),  TEXT("D")},  {TEXT("A"), TEXT("C"), TEXT("A"), TEXT("C"), TEXT("")}, KeepEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("BC"), TEXT("DA")}, {TEXT("A"), TEXT("D")},                                 SkipEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("BC"), TEXT("DA")}, {TEXT("A"), TEXT(""), TEXT(""), TEXT("D")},             KeepEmpty);

	RunParseTokensTest(TEXT("AbCdaBcDAbCd"), {TEXT("Bc"), TEXT("da")}, {TEXT("AbC"), TEXT("DAbCd")}, SkipEmpty);
	RunParseTokensTest(TEXT("AbCdaBcDAbCd"), {TEXT("Bc"), TEXT("da")}, {TEXT("A"), TEXT("d")}, SkipEmpty | IgnoreCase);

	RunParseTokensTest(TEXT("A\u2022\u2022B,,C"), {TEXT(",,"), TEXT("\u2022\u2022")},                      {TEXT("A"), TEXT("B"), TEXT("C")});
	RunParseTokensTest(TEXT("A\u2022\u2022B\u0085\u0085C"), {TEXT("\u0085\u0085"), TEXT("\u2022\u2022")},  {TEXT("A"), TEXT("B"), TEXT("C")});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringParseTokensByCharTest, "System.Core.String.ParseTokens.ByChar", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool StringParseTokensByCharTest::RunTest(const FString& Parameters)
{
	using EOptions = UE::String::EParseTokensOptions;

	auto RunParseTokensTest = [this](
		FStringView View,
		std::initializer_list<TCHAR> Delimiters,
		std::initializer_list<FStringView> ExpectedTokens,
		EOptions Options = EOptions::None)
	{
		TArray<FStringView, TInlineAllocator<8>> ResultTokens;
		if (GetNum(Delimiters) == 1)
		{
			UE::String::ParseTokens(View, GetData(Delimiters)[0], ResultTokens, Options);
		}
		else
		{
			UE::String::ParseTokensMultiple(View, Delimiters, ResultTokens, Options);
		}
		if (!Algo::Compare(ResultTokens, ExpectedTokens))
		{
			TStringBuilder<512> Error;
			Error << TEXTVIEW("UE::String::ParseTokens[Multiple] failed to parse \"") << View << TEXTVIEW("\" with delimiters {");
			Error.JoinQuoted(Delimiters, TEXTVIEW(", "), TEXTVIEW("'"));
			Error << TEXTVIEW("} result {");
			Error.JoinQuoted(ResultTokens, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("} expected {");
			Error.JoinQuoted(ExpectedTokens, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("}");
			AddError(Error.ToString());
		}
	};

	constexpr EOptions KeepEmpty = EOptions::None;
	constexpr EOptions SkipEmpty = EOptions::SkipEmpty;
	constexpr EOptions IgnoreCase = EOptions::IgnoreCase;
	constexpr EOptions Trim = EOptions::Trim;

	RunParseTokensTest(TEXT(""),         {},                       {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(""),         {},                       {TEXT("")},                         KeepEmpty);
	RunParseTokensTest(TEXT("ABC"),      {},                       {TEXT("ABC")});

	RunParseTokensTest(TEXT(""),         {TEXT(',')},              {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(""),         {TEXT(',')},              {TEXT("")},                         KeepEmpty);
	RunParseTokensTest(TEXT(","),        {TEXT(',')},              {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(","),        {TEXT(',')},              {TEXT(""), TEXT("")},               KeepEmpty);
	RunParseTokensTest(TEXT(",,"),       {TEXT(',')},              {},                                 SkipEmpty);
	RunParseTokensTest(TEXT(",,"),       {TEXT(',')},              {TEXT(""), TEXT(""), TEXT("")},     KeepEmpty);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(',')},              {TEXT(" ")},                        SkipEmpty);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(',')},              {},                                 SkipEmpty | Trim);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(',')},              {TEXT(""), TEXT(" "), TEXT("")},    KeepEmpty);
	RunParseTokensTest(TEXT(", ,"),      {TEXT(',')},              {TEXT(""), TEXT(""), TEXT("")},    KeepEmpty | Trim);
	RunParseTokensTest(TEXT("ABC"),      {TEXT(',')},              {TEXT("ABC")});
	RunParseTokensTest(TEXT("A,,C"),     {TEXT(',')},              {TEXT("A"), TEXT("C")},             SkipEmpty);
	RunParseTokensTest(TEXT("A,,C"),     {TEXT(',')},              {TEXT("A"), TEXT(""), TEXT("C")},   KeepEmpty);
	RunParseTokensTest(TEXT("A,\tB\t,C"), {TEXT(',')},             {TEXT("A"), TEXT("\tB\t"), TEXT("C")});
	RunParseTokensTest(TEXT(",A, B ,C,"), {TEXT(',')},             {TEXT("A"), TEXT(" B "), TEXT("C")},                     SkipEmpty);
	RunParseTokensTest(TEXT(",A, B ,C,"), {TEXT(',')},             {TEXT(""), TEXT("A"), TEXT(" B "), TEXT("C"), TEXT("")}, KeepEmpty);
	RunParseTokensTest(TEXT("A\u2022B\u2022C"), {TEXT('\u2022')},  {TEXT("A"), TEXT("B"), TEXT("C")});

	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT('B'),  TEXT('D')},                  {TEXT("A"), TEXT("C"), TEXT("A"), TEXT("C")},           SkipEmpty);
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT('B'),  TEXT('D')},                  {TEXT("A"), TEXT("C"), TEXT("A"), TEXT("C"), TEXT("")}, KeepEmpty);
	RunParseTokensTest(TEXT("A\u2022B,C"), {TEXT(','), TEXT('\u2022')},            {TEXT("A"), TEXT("B"), TEXT("C")});
	RunParseTokensTest(TEXT("A\u2022B\u0085C"), {TEXT('\u0085'), TEXT('\u2022')},  {TEXT("A"), TEXT("B"), TEXT("C")});

	RunParseTokensTest(TEXT("ABC"), {TEXT('b')}, {TEXT("ABC")}, SkipEmpty);
	RunParseTokensTest(TEXT("ABC"), {TEXT('b')}, {TEXT("A"), TEXT("C")}, SkipEmpty | IgnoreCase);
	RunParseTokensTest(TEXT("AbCdaBcD"), {TEXT('B'),  TEXT('d')}, {TEXT("AbC"), TEXT("A"), TEXT("cD")}, SkipEmpty);
	RunParseTokensTest(TEXT("AbCdaBcD"), {TEXT('B'),  TEXT('d')}, {TEXT("A"), TEXT("C"), TEXT("a"), TEXT("c")}, SkipEmpty | IgnoreCase);
	RunParseTokensTest(TEXT("A\u2022B\u2022C"), {TEXT('\u2022'), TEXT('b')}, {TEXT("A"), TEXT("B"), TEXT("C")}, SkipEmpty);
	RunParseTokensTest(TEXT("A\u2022B\u2022C"), {TEXT('\u2022'), TEXT('b')}, {TEXT("A"), TEXT("C")}, SkipEmpty | IgnoreCase);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
