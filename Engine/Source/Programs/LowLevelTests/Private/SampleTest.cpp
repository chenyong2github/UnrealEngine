// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "TestHarness.h"

TEST_CASE("Sample test case", "[sample]")
{
	SECTION("Smoke")
	{
		REQUIRE(1 == 1);
	}
}