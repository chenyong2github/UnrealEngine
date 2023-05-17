// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "ColorSpace.h"
#include "Logging/LogMacros.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "OpenColorIOWrapper.h"
#include "Engine/Texture.h"
#include "TransferFunctions.h"

#if WITH_DEV_AUTOMATION_TESTS

#if WITH_OCIO
THIRD_PARTY_INCLUDES_START
#include "OpenColorIO/OpenColorIO.h"
THIRD_PARTY_INCLUDES_END
#endif

DEFINE_LOG_CATEGORY_STATIC(LogUnrealOpenColorIOTest, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOpenColorIOTransferFunctionsTest, "System.OpenColorIO.DecodeToWorkingColorSpace", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FOpenColorIOTransferFunctionsTest::RunTest(const FString& Parameters)
{
	bool bSuccessful = true;

#if WITH_OCIO
	using namespace OCIO_NAMESPACE;
	using namespace UE::Color;

	const FLinearColor TestColor = FLinearColor(0.9f, 0.5f, 0.2f, 1.0f);
	FOpenColorIOEngineBuiltInConfigWrapper TestConfig = {};

	for (uint8 TestEncoding = static_cast<uint8>(ETextureSourceEncoding::TSE_None); TestEncoding < static_cast<uint8>(ETextureSourceEncoding::TSE_MAX); ++TestEncoding)
	{
		FLinearColor Expected = UE::Color::Decode(static_cast<EEncoding>(TestEncoding), TestColor);

		FTextureSourceColorSettings TestSettings = {};
		TestSettings.EncodingOverride = static_cast<ETextureSourceEncoding>(TestEncoding);
		TestSettings.ColorSpace = ETextureColorSpace::TCS_None;

		FOpenColorIOProcessorWrapper Processor = TestConfig.GetProcessorFromTextureColorSettings(TestSettings);
		FOpenColorIOCPUProcessorWrapper ProcessorCPU(Processor);

		FLinearColor Actual = TestColor;
		ProcessorCPU.TransformColor(Actual);

		// Note: We make the tolerance relative to the values themselves to account for larger values in PQ.
		const float Tolerance = UE_KINDA_SMALL_NUMBER * 0.5f * (Actual.R + Expected.R);

		if (!Actual.Equals(Expected, Tolerance))
		{
			const FString TestNameToPrint = FString::Printf(TEXT("OpenColorIO: %u:%u"), (uint32)TestSettings.EncodingOverride, (uint32)TestSettings.ColorSpace);
			AddError(FString::Printf(TEXT("Expected '%s' to be %s, but it was %s."), *TestNameToPrint, *Expected.ToString(), *Actual.ToString()), 1);
			bSuccessful = false;
		}
	}
#endif

	return bSuccessful;
}

#endif