// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseLines.h"

#include "Algo/Compare.h"
#include "Containers/StringView.h"
#include "Misc/AutomationTest.h"
#include "Misc/StringBuilder.h"
#include "Templates/EqualTo.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringParseLinesTest, "System.Core.String.ParseLines", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool StringParseLinesTest::RunTest(const FString& Parameters)
{
	using EOptions = UE::String::EParseLinesOptions;

	auto RunParseLinesTest = [this](FStringView View, std::initializer_list<FStringView> ExpectedLines, EOptions Options = EOptions::None)
	{
		TArray<FStringView, TInlineAllocator<8>> ResultLines;
		UE::String::ParseLines(View, ResultLines, Options);
		if (!Algo::Compare(ResultLines, ExpectedLines))
		{
			TStringBuilder<512> Error;
			Error << TEXTVIEW("UE::String::ParseLines failed to parse \"") << FString(View).ReplaceCharWithEscapedChar() << TEXTVIEW("\" result {");
			Error.JoinQuoted(ResultLines, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("} expected {");
			Error.JoinQuoted(ExpectedLines, TEXTVIEW(", "), TEXTVIEW("\""));
			Error << TEXTVIEW("}");
			AddError(Error.ToString());
		}
	};

	constexpr EOptions KeepEmpty = EOptions::None;
	constexpr EOptions SkipEmpty = EOptions::SkipEmpty;
	constexpr EOptions Trim = EOptions::Trim;

	RunParseLinesTest(TEXTVIEW(""), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW(""), {TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\n"), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\n"), {TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r"), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r"), {TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\n"), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\n"), {TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\n\n"), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\n\n"), {TEXTVIEW(""), TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\r"), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\r"), {TEXTVIEW(""), TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\n\r\n"), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\n\r\n"), {TEXTVIEW(""), TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC").Left(2), {}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC").Left(2), {TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC\r\nDEF").Left(5), {TEXTVIEW("ABC")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC\r\nDEF").Left(5), {TEXTVIEW(""), TEXTVIEW("ABC")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("ABC DEF"), {TEXTVIEW("ABC DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\nABC DEF\n"), {TEXTVIEW("ABC DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\nABC DEF\n"), {TEXTVIEW(""), TEXTVIEW("ABC DEF")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\rABC DEF\r"), {TEXTVIEW("ABC DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\rABC DEF\r"), {TEXTVIEW(""), TEXTVIEW("ABC DEF")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC DEF\r\n"), {TEXTVIEW("ABC DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC DEF\r\n"), {TEXTVIEW(""), TEXTVIEW("ABC DEF")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\n\r\nABC DEF\r\n\r\n"), {TEXTVIEW("ABC DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\n\r\nABC DEF\r\n\r\n"), {TEXTVIEW(""), TEXTVIEW(""), TEXTVIEW("ABC DEF"), TEXTVIEW("")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("ABC\nDEF"), {TEXTVIEW("ABC"), TEXTVIEW("DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("ABC\rDEF"), {TEXTVIEW("ABC"), TEXTVIEW("DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC\r\nDEF\r\n"), {TEXTVIEW("ABC"), TEXTVIEW("DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC\r\nDEF\r\n"), {TEXTVIEW(""), TEXTVIEW("ABC"), TEXTVIEW("DEF")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC\r\n\r\nDEF\r\n"), {TEXTVIEW("ABC"), TEXTVIEW("DEF")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW("\r\nABC\r\n\r\nDEF\r\n"), {TEXTVIEW(""), TEXTVIEW("ABC"), TEXTVIEW(""), TEXTVIEW("DEF")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW(" \t\r\n\t ABC \t\r\n\t \t\r\n\t DEF \t\r\n"), {TEXTVIEW(" \t"), TEXTVIEW("\t ABC \t"), TEXTVIEW("\t \t"), TEXTVIEW("\t DEF \t")}, SkipEmpty);
	RunParseLinesTest(TEXTVIEW(" \t\r\n\t ABC \t\r\n\t \t\r\n\t DEF \t\r\n"), {TEXTVIEW("ABC"), TEXTVIEW("DEF")}, SkipEmpty | Trim);
	RunParseLinesTest(TEXTVIEW(" \t\r\n\t ABC \t\r\n\t \t\r\n\t DEF \t\r\n"), {TEXTVIEW(" \t"), TEXTVIEW("\t ABC \t"), TEXTVIEW("\t \t"), TEXTVIEW("\t DEF \t")}, KeepEmpty);
	RunParseLinesTest(TEXTVIEW(" \t\r\n\t ABC \t\r\n\t \t\r\n\t DEF \t\r\n"), {TEXTVIEW(""), TEXTVIEW("ABC"), TEXTVIEW(""), TEXTVIEW("DEF")}, KeepEmpty | Trim);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
