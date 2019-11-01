// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapLightEstimationTypes.h"
#include "MagicLeapLightEstimationFunctionLibrary.generated.h"

UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPLIGHTESTIMATION_API UMagicLeapLightEstimationFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
		Create a light estimation tracker.
		@return true if tracker was successfully created, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "LightEstimation Function Library | MagicLeap")
	static bool CreateTracker();

	/** Destroy a light estimation tracker. */
	UFUNCTION(BlueprintCallable, Category = "LightEstimation Function Library | MagicLeap")
	static void DestroyTracker();

	/**
		Check if a light estimation tracker has already been created.
		@return true if tracker already exists and is valid, false otherwise
	*/
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "LightEstimation Function Library | MagicLeap")
	static bool IsTrackerValid();

	/**
		Gets information about the ambient light sensor global state.
		@note Capturing images or video will stop the lighting information update, causing the retrieved data to be stale (old timestamps).
		@param GlobalAmbientState Output param containing the information about the global lighting state (ambient intensity). Valid only if return value of function is true.
		@return true if the global ambient state was succesfully retrieved.
	*/
	UFUNCTION(BlueprintCallable, Category = "LightEstimation Function Library | MagicLeap")
	static bool GetAmbientGlobalState(FMagicLeapLightEstimationAmbientGlobalState& GlobalAmbientState);

	/**
		Gets information about the color temperature state.
		@note Capturing images or video will stop the lighting information update, causing the retrieved data to be stale (old timestamps).
		@param ColorTemperatureState Output param containing the information about the color temperature. Valid only if return value of function is true.
		@return true if the color temperature state was succesfully retrieved.
	*/
	UFUNCTION(BlueprintCallable, Category = "LightEstimation Function Library | MagicLeap")
	static bool GetColorTemperatureState(FMagicLeapLightEstimationColorTemperatureState& ColorTemperatureState);
};
