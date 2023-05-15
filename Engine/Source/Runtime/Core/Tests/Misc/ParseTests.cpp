// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_LOW_LEVEL_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/Parse.h"

#include "Tests/TestHarnessAdapter.h"

TEST_CASE("Parse::Value::ToBuffer", "[Parse][Smoke]")
{
	TCHAR Buffer[256];

	SECTION("Basic Usage") 
	{
		const TCHAR* Line = TEXT("a=a1 b=b2 c=c3");

		CHECK(FParse::Value(Line, TEXT("a="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("a1")) == 0);

		CHECK(FParse::Value(Line, TEXT("b="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("b2")) == 0);

		CHECK(FParse::Value(Line, TEXT("c="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("c3")) == 0);

		CHECK(false == FParse::Value(Line, TEXT("not_there="), Buffer, 256));
		CHECK(Buffer[0] == TCHAR(0));
	}

	SECTION("Quoted Values")
	{
		CHECK(FParse::Value(TEXT("a=a1 b=\"value with a space, and commas\" c=c3"), TEXT("b="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("value with a space, and commas")) == 0);
	}

	SECTION("Value may (not)? have a delimiter")
	{
		const TCHAR* Line = TEXT("a=a1,a2");

		CHECK(FParse::Value(Line, TEXT("a="), Buffer, 256, true));
		CHECK(FCString::Strcmp(Buffer, TEXT("a1")) == 0);

		CHECK(FParse::Value(Line, TEXT("a="), Buffer, 256, false)); // false = don't stop on , or )
		CHECK(FCString::Strcmp(Buffer, TEXT("a1,a2")) == 0);
	}

	SECTION("Value may have spaces on its left")
	{
		CHECK(FParse::Value(TEXT("a=   value"), TEXT("a="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("value")) == 0);
	}

	SECTION("Value could be a key value pair")
	{
		CHECK(FParse::Value(TEXT("a=  b=value"), TEXT("a="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("b=value")) == 0);

		CHECK(FParse::Value(TEXT("a=  b=  value"), TEXT("a="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("b=")) == 0);
		CHECK(FParse::Value(TEXT("a=  b=  value"), TEXT("b="), Buffer, 256));
		CHECK(FCString::Strcmp(Buffer, TEXT("value")) == 0);
	}

	SECTION("Key may appear mutiple times")
	{
		const TCHAR* Line = TEXT("rep=a1 rep=b2 rep=c3");
		const TCHAR* ExpectedResults[] = { TEXT("a1"), TEXT("b2"), TEXT("c3") };

		const TCHAR* Cursor = Line;
		for (int Loop = 0; Loop < 4; ++Loop)
		{
			CHECK(Cursor != nullptr);

			bool bFound = FParse::Value(Cursor, TEXT("rep="), Buffer, 256, true, &Cursor);

			if (Loop < 3) 
			{
				CHECK(bFound);
				CHECK(FCString::Strcmp(Buffer, ExpectedResults[Loop]) == 0);
			}
			else
			{
				CHECK(!bFound);
				CHECK(Buffer[0] == TCHAR(0));
				CHECK(Cursor == nullptr);
			}
		}
	}
	
	SECTION("Key may have no value, It is found but Value is empty")
	{
		CHECK(FParse::Value(TEXT("a=   "), TEXT("a="), Buffer, 256));
		CHECK(Buffer[0] == TCHAR(0));
	}

	SECTION("Key with unbalanced quote, It is found but Value is empty")
	{
		for (TCHAR& C : Buffer)
		{
			C = TCHAR('*');
		}
		CHECK(FParse::Value(TEXT("a=\"   "), TEXT("a="), Buffer, 256));
		CHECK(FCString::Strchr(Buffer, TCHAR('*')) == nullptr);
	}

	SECTION("Key may have no value, It is found but Value is empty")
	{
		CHECK(FParse::Value(TEXT("a=   "), TEXT("a="), Buffer, 256));
		CHECK(Buffer[0] == TCHAR(0));
	}

	SECTION("Output var sanity")
	{
		CHECK(false == FParse::Value(TEXT("a=   "), TEXT("a="), Buffer, 0));
	}
}

#endif
	