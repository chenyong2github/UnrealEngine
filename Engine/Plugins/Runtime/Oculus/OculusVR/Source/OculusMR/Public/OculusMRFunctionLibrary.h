// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "UObject/ObjectMacros.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "OculusMRFunctionLibrary.generated.h"

class USceneComponent;
class UOculusMR_Settings;
struct FTrackedCamera;

namespace OculusHMD
{
	class FOculusHMD;
}

UCLASS()
class OCULUSMR_API UOculusMRFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	// Get the OculusMR settings object
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR", meta = (DisplayName = "Get Oculus MR Settings"))
	static UOculusMR_Settings* GetOculusMRSettings();

	// Get the component that the OculusMR camera is tracking. When this is null, the camera will track the player pawn.
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR")
	static USceneComponent* GetTrackingReferenceComponent();
 
	// Set the component for the OculusMR camera to track. If this is set to null, the camera will track the player pawn.
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR")
	static bool SetTrackingReferenceComponent(USceneComponent* Component);

	// Get the scaling factor for the MRC configuration. Returns 0 if not available.
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR", meta = (DisplayName = "Get MRC Scaling Factor"))
	static float GetMrcScalingFactor();

	// Set the scaling factor for the MRC configuration. This should be a positive value set to the same scaling as the VR player pawn so that the game capture and camera video are aligned.
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR", meta = (DisplayName = "Set MRC Scaling Factor"))
	static bool SetMrcScalingFactor(float ScalingFactor = 1.0f);

	// Check if MRC is enabled
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR")
	static bool IsMrcEnabled();

	// Check if MRC is enabled and actively capturing
	UFUNCTION(BlueprintCallable, Category = "OculusLibrary|MR")
	static bool IsMrcActive();

public:

	static class OculusHMD::FOculusHMD* GetOculusHMD();

	/** Retrieve an array of all (calibrated) tracked cameras which were calibrated through the CameraTool */
	static void GetAllTrackedCamera(TArray<FTrackedCamera>& TrackedCameras, bool bCalibratedOnly = true);

	static bool GetTrackingReferenceLocationAndRotationInWorldSpace(USceneComponent* TrackingReferenceComponent, FVector& TRLocation, FRotator& TRRotation);
};
