// Copyright Epic Games, Inc. All Rights Reserved.

#include "Animation/VariableFrameStrippingSettings.h"
#include "Interfaces/ITargetPlatform.h"
#include "Animation/AnimationSettings.h"
#include "AnimationUtils.h"
#include "AnimationCompression.h"
#include "PlatformInfo.h"

UVariableFrameStrippingSettings::UVariableFrameStrippingSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UseVariableFrameStripping = FPerPlatformBool(false);
	FrameStrippingRate = 3;
}

#if WITH_EDITORONLY_DATA
/** Generates a DDC key that takes into account the current settings, selected codec, input anim sequence and TargetPlatform */
void UVariableFrameStrippingSettings::PopulateDDCKey(const UE::Anim::Compression::FAnimDDCKeyArgs& KeyArgs, FArchive& Ar)
{
	const ITargetPlatform* TargetPlatform = KeyArgs.TargetPlatform;
	if (TargetPlatform != nullptr)
	{
		FName TargetPlatformName = TargetPlatform->GetTargetPlatformInfo().IniPlatformName;
		bool PerPlatformUse = UseVariableFrameStripping.GetValueForPlatform(TargetPlatformName);
		Ar << PerPlatformUse;
		int32 PerPlatformRate = FrameStrippingRate.GetValueForPlatform(TargetPlatformName);
		Ar << PerPlatformRate;
	}
}
#endif