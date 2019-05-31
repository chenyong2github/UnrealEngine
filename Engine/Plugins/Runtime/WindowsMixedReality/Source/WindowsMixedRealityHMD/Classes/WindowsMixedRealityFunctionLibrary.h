// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#if WITH_WINDOWS_MIXED_REALITY
#include "MixedRealityInterop.h"
#include "WindowsMixedRealityInteropUtility.h"
#endif

#include "WindowsMixedRealityFunctionLibrary.Generated.h"

UENUM()
enum class EHMDSpatialLocatability : uint8 
{
	Unavailable = 0,
	OrientationOnly = 1,
	PositionalTrackingActivating = 2,
	PositionalTrackingActive = 3,
	PositionalTrackingInhibited = 4,
};

USTRUCT(BlueprintType)
struct FPointerPoseInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WindowsMixedRealityHMD")
	FVector Origin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WindowsMixedRealityHMD")
	FVector Direction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WindowsMixedRealityHMD")
	FVector Up;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WindowsMixedRealityHMD")
	FQuat Orientation;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FTrackingChangeCallback, EHMDSpatialLocatability, locatability);

/**
* Windows Mixed Reality Extensions Function Library
*/
UCLASS()
class WINDOWSMIXEDREALITYHMD_API UWindowsMixedRealityFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:

	/**
	* Returns name of WindowsMR device type.
	*/
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD")
	static FString GetVersionString();

	/**
	* Sets game context to immersive or slate.
	* immersive: true for immersive context, false for slate.
	*/
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD")
	static void ToggleImmersive(bool immersive);

	/**
	* Returns true if currently rendering immersive, or false if rendering as a slate.
	*/
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD")
	static bool IsCurrentlyImmersive();

	/**
	* Returns true if running on a WMR VR device, false if running on HoloLens.
	*/
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD")
	static bool IsDisplayOpaque();

	/**
	* Locks the mouse cursor to the center of the screen if the hmd is worn.
	* Default is true to help guarantee mouse focus when the hmd is worn.
	* Set this to false to override the default behavior if your application requires free mouse movement.
	* locked: true to lock to center, false to not lock.
	*/
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD")
	static void LockMouseToCenter(bool locked);

	
	/**
	 * Returns true if a WMR VR device or HoloLens are tracking the environment.
	 */
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD")
	static bool IsTrackingAvailable();

	/**
	 * Returns the pose information to determine what a WMR device is pointing at.
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD")
	static FPointerPoseInfo GetPointerPoseInfo(EControllerHand hand);
};
