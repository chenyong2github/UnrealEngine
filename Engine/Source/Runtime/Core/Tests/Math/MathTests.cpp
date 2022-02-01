// Copyright Epic Games, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "Math/Color.h"
#include "TestHarness.h"

TEST_CASE("Core::Math::FLinearColor::Smoke Test", "[Core][Math][Smoke]")
{
	SECTION("FLinearColor")
	{
		FLinearColor black(FLinearColor::Black);
		FLinearColor white(FLinearColor::White);
		FLinearColor red(FLinearColor::Red);

		REQUIRE(red.R == 1.0);
		REQUIRE(red.G == 0.0);
		REQUIRE(red.B == 0.0);
		REQUIRE(red.A == 1.0);
		REQUIRE(red == FLinearColor::Red);
		REQUIRE(red != FLinearColor::Green);

		REQUIRE(white.IsAlmostBlack() == false);
		REQUIRE(black.IsAlmostBlack() == true);

		FLinearColor yellow(FColor::FromHex(FString("FFFF00FF")));
		FLinearColor green(FColor::FromHex(FString("00FF00FF")));
		FLinearColor blue(FColor::FromHex(FString("0000FFFF")));

		REQUIRE(yellow == FLinearColor::Yellow);
		REQUIRE(green == FLinearColor::Green);
		REQUIRE(blue == FLinearColor::Blue);
		REQUIRE(blue != yellow);
		REQUIRE(green != yellow);
		REQUIRE(yellow == yellow);
	}
}

TEST_CASE("Core::Math::FColor::Smoke Test", "[Core][Math][Smoke]")
{
	SECTION("FColor")
	{
		FColor black(FColor::Black);
		FColor white(FColor::White);
		FColor red(FColor::Red);

		REQUIRE(red.R == 0xFF);
		REQUIRE(red.G == 0x00);
		REQUIRE(red.B == 0x00);
		REQUIRE(red.A == 0xFF);
		REQUIRE(red == FColor::Red);
		REQUIRE(red != FColor::Green);

		FColor yellow(FColor::FromHex(FString("FFFF00FF")));
		FColor green(FColor::FromHex(FString("00FF00FF")));
		FColor blue(FColor::FromHex(FString("0000FFFF")));

		REQUIRE(yellow == FColor::Yellow);
		REQUIRE(green == FColor::Green);
		REQUIRE(blue == FColor::Blue);
		REQUIRE(blue != yellow);
		REQUIRE(green != yellow);
		REQUIRE(yellow == yellow);

		REQUIRE(1 == 1);
	}
}
