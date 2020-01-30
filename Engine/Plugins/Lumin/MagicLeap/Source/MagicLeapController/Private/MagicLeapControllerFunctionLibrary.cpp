// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapControllerFunctionLibrary.h"
#include "IMagicLeapControllerPlugin.h"
#include "MagicLeapController.h"
#include "IMagicLeapPlugin.h"

bool UMagicLeapControllerFunctionLibrary::PlayLEDPattern(FName MotionSource, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->PlayLEDPattern(MotionSource, LEDPattern, LEDColor, DurationInSec);
	}
	return false;
}

bool UMagicLeapControllerFunctionLibrary::PlayLEDEffect(FName MotionSource, EMagicLeapControllerLEDEffect LEDEffect, EMagicLeapControllerLEDSpeed LEDSpeed, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->PlayLEDEffect(MotionSource, LEDEffect, LEDSpeed, LEDPattern, LEDColor, DurationInSec);
	}
	return false;
}

bool UMagicLeapControllerFunctionLibrary::PlayHapticPattern(FName MotionSource, EMagicLeapControllerHapticPattern HapticPattern, EMagicLeapControllerHapticIntensity Intensity)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->PlayHapticPattern(MotionSource, HapticPattern, Intensity);
	}
	return false;
}

bool UMagicLeapControllerFunctionLibrary::SetControllerTrackingMode(EMagicLeapControllerTrackingMode TrackingMode)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->SetControllerTrackingMode(TrackingMode);
	}
	return false;
}

EMagicLeapControllerTrackingMode UMagicLeapControllerFunctionLibrary::GetControllerTrackingMode()
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->GetControllerTrackingMode();
	}
	return EMagicLeapControllerTrackingMode::InputService;
}

FName UMagicLeapControllerFunctionLibrary::GetMotionSourceForHand(EControllerHand Hand)
{
#if WITH_MLSDK
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->ControllerMapper.GetMotionSourceForHand(Hand);
	}
#endif //WITH_MLSDK
	return FMagicLeapMotionSourceNames::Unknown;
}

EControllerHand UMagicLeapControllerFunctionLibrary::GetHandForMotionSource(FName MotionSource)
{
#if WITH_MLSDK
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->ControllerMapper.GetHandForMotionSource(MotionSource);
	}
#endif //WITH_MLSDK
	return EControllerHand::ControllerHand_Count;
}

bool UMagicLeapControllerFunctionLibrary::SetMotionSourceForHand(EControllerHand Hand, FName MotionSource)
{
#if WITH_MLSDK
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		controller->ControllerMapper.MapHandToMotionSource(Hand, MotionSource);
		return true;
	}
#endif //WITH_MLSDK
	return false;
}

EMagicLeapControllerType UMagicLeapControllerFunctionLibrary::GetControllerType(FName MotionSource)
{
#if WITH_MLSDK
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->ControllerMapper.MotionSourceToControllerType(MotionSource);
	}
#endif //WITH_MLSDK
	return EMagicLeapControllerType::None;
}

bool UMagicLeapControllerFunctionLibrary::IsMLControllerConnected(FName MotionSource)
{
#if WITH_MLSDK
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->IsMLControllerConnected(MotionSource);
	}
#endif //WITH_MLSDK
	return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// DEPRECATED FUNCTIONS
/////////////////////////////////////////////////////////////////////////////////////////////
int32 UMagicLeapControllerFunctionLibrary::MaxSupportedMagicLeapControllers()
{
#if WITH_MLSDK
	return MLInput_MaxControllers;
#else
	return 0;
#endif //WITH_MLSDK
}

bool UMagicLeapControllerFunctionLibrary::GetControllerMapping(int32 ControllerIndex, EControllerHand& Hand)
{
	return false;
}

void UMagicLeapControllerFunctionLibrary::InvertControllerMapping()
{
#if WITH_MLSDK
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->ControllerMapper.SwapHands();
	}
#endif //WITH_MLSDK
}

EMagicLeapControllerType UMagicLeapControllerFunctionLibrary::GetMLControllerType(EControllerHand Hand)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->GetMLControllerType(Hand);
	}
	return EMagicLeapControllerType::None;
}

bool UMagicLeapControllerFunctionLibrary::PlayControllerLED(EControllerHand Hand, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->PlayControllerLED(Hand, LEDPattern, LEDColor, DurationInSec);
	}
	return false;
}

bool UMagicLeapControllerFunctionLibrary::PlayControllerLEDEffect(EControllerHand Hand, EMagicLeapControllerLEDEffect LEDEffect, EMagicLeapControllerLEDSpeed LEDSpeed, EMagicLeapControllerLEDPattern LEDPattern, EMagicLeapControllerLEDColor LEDColor, float DurationInSec)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->PlayControllerLEDEffect(Hand, LEDEffect, LEDSpeed, LEDPattern, LEDColor, DurationInSec);
	}
	return false;
}

bool UMagicLeapControllerFunctionLibrary::PlayControllerHapticFeedback(EControllerHand Hand, EMagicLeapControllerHapticPattern HapticPattern, EMagicLeapControllerHapticIntensity Intensity)
{
	TSharedPtr<FMagicLeapController> controller = StaticCastSharedPtr<FMagicLeapController>(IMagicLeapControllerPlugin::Get().GetInputDevice());
	if (controller.IsValid())
	{
		return controller->PlayControllerHapticFeedback(Hand, HapticPattern, Intensity);
	}
	return false;
}
