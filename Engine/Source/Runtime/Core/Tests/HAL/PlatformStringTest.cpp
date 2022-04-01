// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/PlatformString.h"
#include "TestHarness.h"

template <typename CharType, SIZE_T Size>
static void InvokePlatformStringGetVarArgs(CharType (&Dest)[Size], const CharType* Fmt, ...)
{
	va_list ap;
	va_start(ap, Fmt);
	FPlatformString::GetVarArgs(Dest, Size, Fmt, ap);
	va_end(ap);
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::HAL::PlatformString::GetVarArgs", "[Core][HAL][Smoke]")
{
	TCHAR Buffer[128];
	InvokePlatformStringGetVarArgs(Buffer, TEXT("A%.*sZ"), 4, TEXT(" to B"));
	CHECK(!FCString::Strcmp(Buffer, TEXT("A to Z")));
}

TEST_CASE_METHOD(FAutomationTestFixture, "Core::HAL::PlatformString::Strnlen", "[Core][HAL][Smoke]")
{
	CHECK_EQUAL(FPlatformString::Strnlen((const ANSICHAR*)nullptr, 0), 0);
	CHECK_EQUAL(FPlatformString::Strnlen("", 0), 0);
	CHECK_EQUAL(FPlatformString::Strnlen("1", 0), 0);
	CHECK_EQUAL(FPlatformString::Strnlen("1", 1), 1);
	CHECK_EQUAL(FPlatformString::Strnlen("1", 2), 1);
	CHECK_EQUAL(FPlatformString::Strnlen("123", 2), 2);
	ANSICHAR AnsiBuffer[128] = "123456789";
	CHECK_EQUAL(FPlatformString::Strnlen(AnsiBuffer, UE_ARRAY_COUNT(AnsiBuffer)), 9);

	CHECK_EQUAL(FPlatformString::Strnlen((const TCHAR*)nullptr, 0), 0); //-V575
	CHECK_EQUAL(FPlatformString::Strnlen(TEXT(""), 0), 0); //-V575
	CHECK_EQUAL(FPlatformString::Strnlen(TEXT("1"), 0), 0); //-V575
	CHECK_EQUAL(FPlatformString::Strnlen(TEXT("1"), 1), 1);
	CHECK_EQUAL(FPlatformString::Strnlen(TEXT("1"), 2), 1);
	CHECK_EQUAL(FPlatformString::Strnlen(TEXT("123"), 2), 2);
	TCHAR Buffer[128] = {};
	FCString::Strcpy(Buffer, TEXT("123456789"));
	CHECK_EQUAL(FPlatformString::Strnlen(Buffer, UE_ARRAY_COUNT(Buffer)), 9);
}
