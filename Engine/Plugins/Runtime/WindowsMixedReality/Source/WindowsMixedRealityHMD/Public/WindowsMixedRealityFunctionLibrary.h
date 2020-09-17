// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WindowsMixedRealityFunctionLibrary.Generated.h"

UENUM(BlueprintType, Category = "WindowsMixedRealityHMD")
enum class EHMDSpatialLocatability : uint8 
{
	Unavailable = 0,
	OrientationOnly = 1,
	PositionalTrackingActivating = 2,
	PositionalTrackingActive = 3,
	PositionalTrackingInhibited = 4,
};

UENUM(BlueprintType, Category = "WindowsMixedRealityHMD")
enum class EHMDInputControllerButtons : uint8
{
	Select,
	Grasp,
	Menu,
	Thumbstick,
	Touchpad,
	TouchpadIsTouched,
	Count UMETA(Hidden)
};

UENUM()
enum class EHMDTrackingStatus : uint8
{
	NotTracked,
	InertialOnly,
	Tracked
};

USTRUCT(BlueprintType, Category = "WindowsMixedRealityHMD")
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
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WindowsMixedRealityHMD")
	EHMDTrackingStatus TrackingStatus;
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
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "Use Generic HMD library version"))
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
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "Use GetXRSystemFlags and check for bIsAR"))
	static bool IsDisplayOpaque();

	/**
	* Locks the mouse cursor to the center of the screen if the hmd is worn.
	* Default is true to help guarantee mouse focus when the hmd is worn.
	* Set this to false to override the default behavior if your application requires free mouse movement.
	* locked: true to lock to center, false to not lock.
	*/
	UE_DEPRECATED(4.26, "LockMouseToCenter was a dvelopment feature that is no longer needed")
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "No longer needed"))
	static void LockMouseToCenter(bool locked);

	
	/**
	 * Returns true if a WMR VR device or HoloLens are tracking the environment.
	 */
	UFUNCTION(BlueprintPure, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "Use GetHMDData TrackingStatus member"))
	static bool IsTrackingAvailable();

	/**
	 * Returns the pose information to determine what a WMR device is pointing at.
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta=(DeprecatedFunction, DeprecationMessage = "Use GetMotionControllerData Aim members"))
	static FPointerPoseInfo GetPointerPoseInfo(EControllerHand hand);

	/**
	 * Returns true if the button was clicked.
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta=(DeprecatedFunction, DeprecationMessage="Use Input Action Mappings to get events"))
	static bool IsButtonClicked(EControllerHand hand, EHMDInputControllerButtons button);

	/**
	 * Returns true if the button is held down.
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "Use Input Action Mappings to gets events"))
	static bool IsButtonDown(EControllerHand hand, EHMDInputControllerButtons button);

	/**
	 * Returns true if an input device detects a grasp/grab action.
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "Use Input Action Mappings to get left or right grip events.  Also available in GetMotionControllerData"))
	static bool IsGrasped(EControllerHand hand);

	/**
	 * Returns true if a hand or motion controller is experiencing a primary Select press.
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "Use Input Action Mappings to get left or right trigger events"))
	static bool IsSelectPressed(EControllerHand hand);

	/**
	 * Returns tracking state for the controller
	 */
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta=(DeprecatedFunction, DeprecationMessage="Use GetMotionControllerData TrackingStatus"))
	static EHMDTrackingStatus GetControllerTrackingStatus(EControllerHand hand);

	/**
	 *  Set the focus point for the current frame to stabilize your holograms.
	 *  When run on device, the depth buffer with be used.  Use this for remoting.
	 */
	UE_DEPRECATED(4.26, "SetFocusPointForFrame was a dvelopment feature that is no longer needed")
	UFUNCTION(BlueprintCallable, Category = "WindowsMixedRealityHMD", meta = (DeprecatedFunction, DeprecationMessage = "No longer needed"))
	static void SetFocusPointForFrame(FVector position);
};
