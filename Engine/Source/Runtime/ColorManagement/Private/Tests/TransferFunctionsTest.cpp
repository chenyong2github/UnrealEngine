// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "TransferFunctions.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTransferFunctionsTest, "System.ColorManagement.TransferFunctions", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FTransferFunctionsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Color;

	const float TestIncrement = 0.05f;

	// Verify that all transfer functions correctly inverse each other.

	for (uint8 EnumValue = static_cast<uint8>(EEncoding::Linear); EnumValue < static_cast<uint8>(EEncoding::Max); EnumValue++)
	{
		EEncoding EncodingType = static_cast<EEncoding>(EnumValue);

		for (float TestValue = 0.0f; TestValue <= 1.0f; TestValue += TestIncrement)
		{
			float Encoded = UE::Color::Encode(EncodingType, TestValue);
			float Decoded = UE::Color::Decode(EncodingType, Encoded);
			TestEqual(TEXT("Transfer function encode followed by decode must match identity"), Decoded, TestValue, KINDA_SMALL_NUMBER);
		}
	}
	return true;
}

#endif