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

	// The ARPin related functions here are all deprecated in favor of cross-platform friendly versions defined in ARBlueprintLibrary.
	// The new versions also split the concept of the save name from the anchorID, because some platforms do not have anchorID strings.
	// Functionalty should be migrated to the new system to enable cross platform operation and cooking.  These deprecated functions will
	// eventually be removed.

	/**
	 * Create an UARPin with the specified name, which will also be the name used to store it in the Windows Mixed Reality Anchor Store.
	 *
	 * @param Name						The name of the anchor.  If the Name is already in use creation will fail.  A leading "_" is reserved for automatically named anchors. Do not start your names with an underscore.  The name 'None' is illegal.
	 * @param PinToWorldTransform		The Pin which the component will be updated by.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all", DeprecatedFunction, DeprecationMessage = "Please transition to use the functions in ARBlueprintLibrary which save pins to the anchor store by a 'saveId' rather than using the built in WMR anchorId."))
	static UWMRARPin* CreateNamedARPin(FName Name, const FTransform& PinToWorldTransform);

	/**
	 * Load all ARPins from the Windows Mixed Reality Anchor Store.
	 * Note: Pins of the same name as anchor store pins will be overwritten by the anchor store pin.
	 *
	 * @return					Array of Pins that were loaded.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all", DeprecatedFunction, DeprecationMessage = "Please use LoadARPinsFromLocalStore"))
	static TArray<UWMRARPin*> LoadWMRAnchorStoreARPins();
	/**
	 * Save an ARPin to the the Windows Mixed Reality Anchor Store.
	 *
	 * @return					True if saved successfully.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all", DeprecatedFunction, DeprecationMessage = "Please use SaveARPinToLocalStore"))
	static bool SaveARPinToWMRAnchorStore(UARPin* InPin);

	/**
	 * Remove an ARPin from the the Windows Mixed Reality Anchor Store.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR|ARPin", meta = (Keywords = "hololensar wmr pin ar all", DeprecatedFunction, DeprecationMessage = "Please use RemoveARPinFromLocalStore"))
	static void RemoveARPinFromWMRAnchorStore(UARPin* InPin);
	
	/**
	 * Enable or disable Mixed Reality Capture camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all", DeprecatedFunction, DeprecationMessage = "Use SetEnabledXRCamera"))
	static void SetEnabledMixedRealityCamera(bool IsEnabled);

	/**
	 * Change screen size of Mixed Reality Capture camera.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all", DeprecatedFunction, DeprecationMessage = "Use ResizeXRCamera"))
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
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all", DeprecatedFunction, DeprecationMessage = "Use ToggleARCapture"))
	static bool StartCameraCapture();

	/**
	 * Turn the camera off.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all", DeprecatedFunction, DeprecationMessage = "Use ToggleARCapture"))
	static bool StopCameraCapture();
	
	/**
	 * Start looking for QRCodes.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all", DeprecatedFunction, DeprecationMessage = "Use ToggleARCapture"))
	static bool StartQRCodeCapture();

	/**
	 * Stop looking for QRCodes.
	 */
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar wmr ar all", DeprecatedFunction, DeprecationMessage = "Use ToggleARCapture"))
	static bool StopQRCodeCapture();
	
	/**
	 * Show on screen system keyboard.
	 */
	UE_DEPRECATED(4.26, "HoloLens Keyboard should be automatically shown and hidden.")
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (DeprecatedFunction, DeprecationMessage = "The keyboard should be auto-shown and hidden", Keywords = "hololensar wmr ar all"))
	static bool ShowKeyboard();

	/**
	 * Hide on screen system keyboard.
	 */
	UE_DEPRECATED(4.26, "HoloLens Keyboard should be automatically shown and hidden.")
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (DeprecatedFunction, DeprecationMessage = "The keyboard should be auto-shown and hidden", Keywords = "hololensar wmr ar all"))
	static bool HideKeyboard();

	static UWMRARPin* CreateNamedARPinAroundAnchor(FName Name, const FString& AnchorId);

	// Use the legacy MRMesh support for rendering the hand tracker.  Otherwise, default to XRVisualization.
	UFUNCTION(BlueprintCallable, Category = "HoloLensAR", meta = (Keywords = "hololensar hand mesh ar all"))
	static void SetUseLegacyHandMeshVisualization(bool UseLegacyHandMeshVisualization);
};