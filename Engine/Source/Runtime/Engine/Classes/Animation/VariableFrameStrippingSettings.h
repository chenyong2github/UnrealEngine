// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VariableFrameStrippingSettings.generated.h"

/*
* This is a wrapper for the Varrible frame stripping Codec.
* It allows for the mass changing of settings on animation sequences in an editor acessable way.
*/
UCLASS(hidecategories = Object)
class ENGINE_API UVariableFrameStrippingSettings : public UObject
{
	GENERATED_UCLASS_BODY()

	/**
	* Enables the change from standard 1/2 frame stripping to stripping a higher amount of frames per frame kept
	*/
	UPROPERTY(Category = Compression, EditAnywhere)
		FPerPlatformBool UseVariableFrameStripping;

	/**
	* The number of Frames to strip for every one you keep.
	* Allows for overrides of that multiplier.
	*/
	UPROPERTY(Category = Compression, EditAnywhere, meta = (ClampMin = "1"))
		FPerPlatformInt FrameStrippingRate;


#if WITH_EDITORONLY_DATA
	/** Generates a DDC key that takes into account the current settings, selected codec, input anim sequence and TargetPlatform */
	void PopulateDDCKey(const UE::Anim::Compression::FAnimDDCKeyArgs& KeyArgs, FArchive& Ar);

#endif
};
