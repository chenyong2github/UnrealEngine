// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UObject/Package.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "GenericPlatform/IInputInterface.h"
#include "IMagicLeapTrackerEntity.h"
#include "IHapticDevice.h"
#include "IMotionController.h"
#include "MagicLeapControllerKeys.h"
#include "MagicLeapControllerFunctionLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Lumin/CAPIShims/LuminAPI.h"

DEFINE_LOG_CATEGORY_STATIC(LogMagicLeapControllerTest, Display, All);

//function to force the linker to include this cpp
void MagicLeapTestReferenceFunction()
{

}

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_LUMIN

/**
* Play Pattern Haptic Effect
*/
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FPlayPatternHapticLatentCommand, FName, Hand, EMagicLeapControllerHapticPattern, Pattern, EMagicLeapControllerHapticIntensity, Intensity);
bool FPlayPatternHapticLatentCommand::Update()
{
	FString PatternName;
	UEnum* PatternEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerHapticPattern"), true);
	if (PatternEnum != nullptr)
	{
		PatternName = PatternEnum->GetNameByValue((int32)Pattern).ToString();
	}

	FString IntensityName;
	UEnum* IntensityEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerHapticIntensity"), true);
	if (IntensityEnum != nullptr)
	{
		IntensityName = IntensityEnum->GetNameByValue((int32)Intensity).ToString();
	}

	UE_LOG(LogCore, Log, TEXT("FPlayPatternHapticLatentCommand %s, %s %s"), *Hand.ToString(), *PatternName, *IntensityName);

	UMagicLeapControllerFunctionLibrary::PlayHapticPattern(Hand, Pattern, Intensity);

	return true;
}


//@TODO - remove editor
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMagicLeapControllerHapticTest, "System.VR.MagicLeap.Haptics.Patterns", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMagicLeapControllerHapticTest::RunTest(const FString&)
{
	UEnum* PatternEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerHapticPattern"), true);
	UEnum* IntensityEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerHapticIntensity"), true);

	float ActiveDuration = 2.0f;
	//for (uint32 HandIndex = 0; HandIndex < MLInput_MaxControllers; ++HandIndex)
	//for now only right hand matters
	FName MotionSource = TEXT("Right");
	{
		// haptic pattern 0 is None, skip it
		for (uint32 PatternIndex = 1; PatternIndex < PatternEnum->GetMaxEnumValue(); ++PatternIndex)
		{
			for (uint32 IntensityIndex = 0; IntensityIndex < IntensityEnum->GetMaxEnumValue(); ++IntensityIndex)
			{
				//Turn on haptics
				ADD_LATENT_AUTOMATION_COMMAND(FPlayPatternHapticLatentCommand(
					MotionSource,
					(EMagicLeapControllerHapticPattern)PatternIndex,
					(EMagicLeapControllerHapticIntensity)IntensityIndex));
				//Give the command a chance to finish
				ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(ActiveDuration));
			}
		}
	}

	return true;
}


/**
* Play LED Pattern
*/
DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(FPlayLEDPatternLatentCommand, FName, Hand, EMagicLeapControllerLEDPattern, Pattern, EMagicLeapControllerLEDColor, Color, float, Duration);
bool FPlayLEDPatternLatentCommand::Update()
{
	FString PatternName;
	UEnum* PatternEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDPattern"), true);
	if (PatternEnum != nullptr)
	{
		PatternName = PatternEnum->GetNameByValue((int32)Pattern).ToString();
	}

	FString ColorName;
	UEnum* ColorEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDColor"), true);
	if (ColorEnum != nullptr)
	{
		ColorName = ColorEnum->GetNameByValue((int32)Color).ToString();
	}

	UE_LOG(LogCore, Log, TEXT("FPlayLEDPatternLatentCommand %s Hand, %s %s"), *Hand.ToString(), *PatternName, *ColorName);

	UMagicLeapControllerFunctionLibrary::PlayLEDPattern(Hand, Pattern, Color, Duration);

	return true;
}


//@TODO - remove editor
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMagicLeapControllerLEDPatternTest, "System.VR.MagicLeap.LED.Patterns", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMagicLeapControllerLEDPatternTest::RunTest(const FString&)
{
	UEnum* PatternEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDPattern"), true);
	UEnum* ColorEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDColor"), true);

	float ActiveDuration = 1.0f;
	float InactiveDuration = 0.5f;
	//run through each pattern and run the pattern, wait for it to finish, turn off, wait
	//for (uint32 HandIndex = 0; HandIndex < MLInput_MaxControllers; ++HandIndex)
	//for now only right hand matters
	FName MotionSource = TEXT("Right");
	{
		// led pattern 0 is None, skip it
		for (uint32 PatternIndex = 1; PatternIndex < PatternEnum->GetMaxEnumValue(); ++PatternIndex)
		{
			for (uint32 ColorIndex = 0; ColorIndex < ColorEnum->GetMaxEnumValue(); ++ColorIndex)
			{
				//Turn LED pattern on
				ADD_LATENT_AUTOMATION_COMMAND(FPlayLEDPatternLatentCommand(MotionSource, (EMagicLeapControllerLEDPattern)PatternIndex, (EMagicLeapControllerLEDColor)ColorIndex, ActiveDuration));
				//Give the command a chance to finish
				ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(ActiveDuration));
				//Add a second to delimit between patterns
				ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(InactiveDuration));
			}
		}
	}

	return true;
}


/**
* Play LED Effect
*/
DEFINE_LATENT_AUTOMATION_COMMAND_FIVE_PARAMETER(FPlayLEDEffectLatentCommand, FName, Hand, EMagicLeapControllerLEDEffect, LEDEffect, EMagicLeapControllerLEDSpeed, LEDSpeed, EMagicLeapControllerLEDPattern, Pattern, EMagicLeapControllerLEDColor, Color);
bool FPlayLEDEffectLatentCommand::Update()
{
	FString EffectName;
	UEnum* EffectEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDEffect"), true);
	if (EffectEnum != nullptr)
	{
		EffectName = EffectEnum->GetNameByValue((int32)LEDEffect).ToString();
	}

	FString SpeedName;
	UEnum* SpeedEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDSpeed"), true);
	if (SpeedEnum != nullptr)
	{
		SpeedName = SpeedEnum->GetNameByValue((int32)LEDSpeed).ToString();
	}

	FString PatternName;
	UEnum* PatternEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDPattern"), true);
	if (PatternEnum != nullptr)
	{
		PatternName = PatternEnum->GetNameByValue((int32)Pattern).ToString();
	}

	FString ColorName;
	UEnum* ColorEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDColor"), true);
	if (ColorEnum != nullptr)
	{
		ColorName = ColorEnum->GetNameByValue((int32)Color).ToString();
	}

	UE_LOG(LogCore, Log, TEXT("FPlayLEDEffectLatentCommand %s Hand, $s %s %s %s"), *Hand.ToString(), *EffectName, *SpeedName, *PatternName, *ColorName);

	UMagicLeapControllerFunctionLibrary::PlayLEDEffect(Hand, LEDEffect, LEDSpeed, Pattern, Color, 1.0f);

	return true;
}


//@TODO - remove editor
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMagicLeapControllerLEDEffectTest, "System.VR.MagicLeap.LED.Effects", EAutomationTestFlags::ClientContext | EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMagicLeapControllerLEDEffectTest::RunTest(const FString&)
{
	UEnum* EffectEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDEffect"), true);
	UEnum* SpeedEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDSpeed"), true);
	UEnum* PatternEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDPattern"), true);
	UEnum* ColorEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("EMagicLeapControllerLEDColor"), true);

	float ActiveDuration = 1.0f;
	float InactiveDuration = 0.5f;
	//run through each effect, speed, pattern, color and run the effect, wait for it to finish, turn off, wait
	//for (uint32 HandIndex = 0; HandIndex < MLInput_MaxControllers; ++HandIndex)
	//for now only right hand matters
	FName MotionSource = TEXT("Right");
	{
		for (uint32 EffectIndex = 0; EffectIndex < EffectEnum->GetMaxEnumValue(); ++EffectIndex)
		{
			for (uint32 SpeedIndex = 0; SpeedIndex < SpeedEnum->GetMaxEnumValue(); ++SpeedIndex)
			{
				// led pattern 0 is None, skip it
				for (uint32 PatternIndex = 1; PatternIndex < PatternEnum->GetMaxEnumValue(); ++PatternIndex)
				{
					for (uint32 ColorIndex = 0; ColorIndex < ColorEnum->GetMaxEnumValue(); ++ColorIndex)
					{
						//Turn LED pattern on
						ADD_LATENT_AUTOMATION_COMMAND(FPlayLEDEffectLatentCommand(MotionSource, (EMagicLeapControllerLEDEffect)EffectIndex, (EMagicLeapControllerLEDSpeed)SpeedIndex, (EMagicLeapControllerLEDPattern)PatternIndex, (EMagicLeapControllerLEDColor)ColorIndex));
						//Give the command a chance to finish
						ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(ActiveDuration));
						//Add a second to delimit between patterns
						ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(InactiveDuration));
					}
				}
			}
		}
	}

	return true;
}

#endif