// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Misc/FrameRate.h"

#include "MovieGraphBlueprintLibrary.generated.h"

// Forward Declare
class UMovieGraphOutputSettingNode;

UCLASS(meta = (ScriptName = "MovieGraphLibrary"))
class MOVIERENDERPIPELINECORE_API UMovieGraphBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	* If InNode is valid, inspects the provided OutputsettingNode to determine if it wants to override the
	* Frame Rate, and if so, returns the overwritten frame rate. If nullptr, or it does not have the
	* bOverride_bUseCustomFrameRate flag set, then InDefaultrate is returned.
	* @param	InNode			- Optional, setting to inspect for a custom framerate.
	* @param	InDefaultRate	- The frame rate to use if the node is nullptr or doesn't want to override the rate.
	* @return					- The effective frame rate (taking into account the node's desire to override it). 
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Graph")
	static FFrameRate GetEffectiveFrameRate(UMovieGraphOutputSettingNode* InNode, const FFrameRate& InDefaultRate);
};