// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LatentActionManager.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HoloLensARSystem.h"

#include "HoloLensARFunctionLibrary.generated.h"

/** A function library that provides static/Blueprint functions for HoloLensAR.*/
UCLASS()
class HOLOLENSAR_API UHoloLensARFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Create an UARPin with the specified name, which will also be the name used to store it in the Windows Mixed Reality Anchor Store.
	 *
	 * @param Name						The name of the anchor.  If the Name is already in use creation will fail.  A leading "_" is reserved for automatically named anchors. Do not start your names with an underscore.  The name 'None' is illegal.
	 * @param PinToWorldTransform		The Pin which the component will be updated by.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static UWMRARPin* CreateNamedARPin(FName Name, const FTransform& PinToWorldTransform);

	/**
	 * Associate a component with an ARPin, so that its transform will be updated by the pin.  Any previously associated component will be detached.
	 *
	 * @param ComponentToPin	The Component which will be updated by the Pin.
	 * @param Pin				The Pin which the component will be updated by.
	 *
	 * @return					True if Pin successfully created.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static bool PinComponentToARPin(USceneComponent* ComponentToPin, UWMRARPin* Pin);

	/**
	 * Is the WMRAnchorStore ready to handle calls.
	 *
	 * @return					True if anchor store is ready.
	 */
	UFUNCTION(BlueprintPure, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static bool IsWMRAnchorStoreReady();

	/**
	 * Load all ARPins from the Windows Mixed Reality Anchor Store.
	 * Note: Pins of the same name as anchor store pins will be overwritten by the anchor store pin.
	 *
	 * @return					Array of Pins that were loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static TArray<UWMRARPin*> LoadWMRAnchorStoreARPins();

	/**
	 * Save an ARPin to the the Windows Mixed Reality Anchor Store.
	 *
	 * @return					True if saved successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static bool SaveARPinToWMRAnchorStore(UARPin* InPin);

	/**
	 * Remove an ARPin from the the Windows Mixed Reality Anchor Store.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static void RemoveARPinFromWMRAnchorStore(UARPin* InPin);

	/**
	 * Remove all ARPins from the the Windows Mixed Reality Anchor Store.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all"))
	static void RemoveAllARPinsFromWMRAnchorStore();

	/**
	 * Enable or disable Mixed Reality Capture camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static void SetEnabledMixedRealityCamera(bool IsEnabled);

	/**
	 * Change screen size of Mixed Reality Capture camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static FIntPoint ResizeMixedRealityCamera(const FIntPoint& size);

	/**
	 * Get the transform from PV camera space to Unreal world space.
	 */
	UFUNCTION(BlueprintPure, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static FTransform GetPVCameraToWorldTransform();

	/**
	 * Get the PV Camera intrinsics.
	 */
	UFUNCTION(BlueprintPure, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static bool GetPVCameraIntrinsics(FVector2D& focalLength, int& width, int& height, FVector2D& principalPoint, FVector& radialDistortion, FVector2D& tangentialDistortion);

	/**
	 * Get a ray into the scene from a camera point.
	 * X is left/right
	 * Y is up/down
	 */
	UFUNCTION(BlueprintPure, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static FVector GetWorldSpaceRayFromCameraPoint(FVector2D pixelCoordinate);
	
	/**
	 * Turn the camera on.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static void StartCameraCapture();

	/**
	 * Turn the camera off.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all"))
	static void StopCameraCapture();
	
	
	static UWMRARPin* CreateNamedARPinAroundAnchor(FName Name, const FString& AnchorId);
};