// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/UnrealTemplate.h"
#include "Containers/UnrealString.h"
#include "UObject/NameTypes.h"
#include "TestHarness.h"

struct TestA
{
	virtual ~TestA() {}
	virtual void TestAA() 
	{
		Space[0] = 1;
	}
	uint8 Space[64];
};


struct TestB
{
	virtual ~TestB() {}
	virtual void TestBB() 
	{
		Space[5] = 1;
	}
	uint8 Space[96];
};


struct TestC : public TestA, TestB
{
	int i;
};

TEST_CASE("Core::HAL::PlatformTest::Smoke Test", "[Core][HAL][Smoke]")
{
	PTRINT Offset1 = VTABLE_OFFSET(TestC, TestB);
	PTRINT Offset2 = VTABLE_OFFSET(TestC, TestA);
	CHECK(Offset1 == 64 + sizeof(void*));
	CHECK(Offset2 == 0);
	int32 Test = 0x12345678;
#if PLATFORM_LITTLE_ENDIAN
	CHECK(*(uint8*)&Test == 0x78);
#else
	CHECK(*(uint8*)&Test == 0x12);
#endif

	FPlatformMath::AutoTest();

#if WITH_EDITORONLY_DATA
	CHECK(FPlatformProperties::HasEditorOnlyData());
#else
	CHECK(!FPlatformProperties::HasEditorOnlyData());
#endif

	CHECK(FPlatformProperties::HasEditorOnlyData() != FPlatformProperties::RequiresCookedData());

#if PLATFORM_LITTLE_ENDIAN
	CHECK(FPlatformProperties::IsLittleEndian());
#else
	CHECK(!FPlatformProperties::IsLittleEndian());
#endif
	CHECK(FPlatformProperties::PlatformName());

	CHECK(FString(FPlatformProperties::PlatformName()).Len() > 0); 

	static_assert(alignof(int32) == 4, "Align of int32 is not 4."); //Hmmm, this would be very strange, ok maybe, but strange

	MS_ALIGN(16) struct FTestAlign
	{
		uint8 Test;
	} GCC_ALIGN(16);

	static_assert(alignof(FTestAlign) == 16, "Align of FTestAlign is not 16.");

	FName::AutoTest();
}
