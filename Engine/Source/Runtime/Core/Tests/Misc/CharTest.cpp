// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/Char.h"
#include <locale.h>
#include <ctype.h>
#include <wctype.h>
#include "TestHarness.h"

namespace crt
{
	int tolower(ANSICHAR c) { return ::tolower(c); }
	int toupper(ANSICHAR c) { return ::toupper(c); }

	int tolower(WIDECHAR c) { return ::towlower(c); }
	int toupper(WIDECHAR c) { return ::towupper(c); }
}

template<typename CharType>
void RunCharTests(uint32 MaxChar)
{
	for (uint32 I = 0; I < MaxChar; ++I)
	{
		CharType C = (CharType)I;
		TestEqual("TChar::ToLower()", TChar<CharType>::ToLower(C), crt::tolower(C));
		TestEqual("TChar::ToUpper()", TChar<CharType>::ToUpper(C), crt::toupper(C));
	}
}

TEST_CASE("Core::Misc::Char::Smoke Test", "[Core][Misc][Perf]")
{
	const char* CurrentLocale = setlocale(LC_CTYPE, nullptr);
	if (CurrentLocale == nullptr)
	{
		TestFalse(FString::Printf(TEXT("Locale is null but should be \"C\". Did something call setlocale()?")), CurrentLocale == nullptr);
	}
	else if (strcmp("C", CurrentLocale))
	{
		TestFalse(FString::Printf(TEXT("Locale is null but should be \"C\". Did something call setlocale()?")), strcmp("C", CurrentLocale)!=0);
	}
	else
	{
		RunCharTests<ANSICHAR>(128);
		RunCharTests<WIDECHAR>(65536);
	}
}