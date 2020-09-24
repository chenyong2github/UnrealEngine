// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MagicLeapARPinTypes.h"
#include "MagicLeapARPinFunctionLibrary.generated.h"

/** Direct API interface for the Magic Leap Persistent AR Pin tracker system. */
UCLASS(ClassGroup = MagicLeap)
class MAGICLEAPARPIN_API UMagicLeapARPinFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	* Create an ARPin tracker.
	* @return Error code representing specific success or failure cases. If code is EMagicLeapPassableWorldError::PrivilegeRequestPending,
	* poll for IsTrackerValid() to check when the privilege is granted and tracker successfully created.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError CreateTracker();

	/**
	* Destroy an ARPin tracker.
	* @return Error code representing specific success or failure cases.,
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError DestroyTracker();

	/** Is an ARPin tracker already created. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ContentPersistence|MagicLeap")
	static bool IsTrackerValid();

	/**
	* Returns the count of currently available AR Pins.
	* @param Count Output param for number of currently available AR Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError GetNumAvailableARPins(int32& Count);

	/**
	* Returns all the AR Pins currently available.
	* @param NumRequested Max number of AR Pins to query. Pass in a negative integer to get all available Pins.
	* @param Pins Output array containing IDs of the found Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError GetAvailableARPins(int32 NumRequested, TArray<FGuid>& Pins);

	/**
	* Returns the Pin closest to the target point passed in.
	* @param SearchPoint Position, in world space, to search the closest Pin to.
	* @param PinID Output param for the ID of the closest Pin. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError GetClosestARPin(const FVector& SearchPoint, FGuid& PinID);

	/**
	* Returns filtered set of Pins based on the informed parameters.
	* @param Query Search parameters
	* @param Pins Output array containing IDs of the found Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError QueryARPins(const FMagicLeapARPinQuery& Query, TArray<FGuid>& Pins);

	/**
	* Returns the position & orientation of the requested Pin in tracking space
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the position of the Pin in tracking space. Valid only if return value is true.
	* @param Orientation Output param for the orientation of the Pin in tracking space. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static bool GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment);

	/**
	* Returns the world position & orientation of the requested Pin.
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the world position of the Pin. Valid only if return value is true.
	* @param Orientation Output param for the world orientation of the Pin. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static bool GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment);

	/**
	* Returns the state of the requested Pin.
	* @param PinID ID of the Pin to get the state for.
	* @param State Output state of the Pin. Valid only if return value is true.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State);

	UFUNCTION(BlueprintCallable, BlueprintPure, meta=(DisplayName="ToString (FMagicLeapARPinState)", CompactNodeTitle="->", ScriptMethod="StateToString"), Category = "ContentPersistence|MagicLeap")
	static FString GetARPinStateToString(const FMagicLeapARPinState& State);

	UFUNCTION(BlueprintCallable, BlueprintPure, meta=(DisplayName="ToString (ARPinId)", CompactNodeTitle="->", ScriptMethod="IdToString"), Category = "ContentPersistence|MagicLeap")
	static FString ARPinIdToString(const FGuid& ARPinId);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ContentPersistence|MagicLeap")
	static bool ParseStringToARPinId(const FString& PinIdString, FGuid& ARPinId);

	/**
	 * Bind a dynamic delegate to the OnMagicLeapARPinUpdated event.
	 * 
	 * The delegate reports 3 arrays for ARPins added, updated and deleted.
	 * Whether a pin is considered updated is determined by whehter any of its state parameters changed a specified delta.
	 * The delta thresholds can be set in Project Settings > MagicLeapARPin Plugin
	 * @param Delegate Delegate to bind
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static void BindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate);

	/**
	 * Unbind a dynamic delegate from the OnMagicLeapARPinUpdated event.
	 * @param Delegate Delegate to unbind
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static void UnBindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate);

	/**
	* Set the filter used to query ARPins at the specified frequency (see UMagicLeapARPinSettings). This will alter the results reported via the OnMagicLeapARPinUpdated delegates only
	* and not the ones by GetClosestARPin() and QueryARPins().
	* By default the filter includes all available Pin in an unbounded distance. If an ARPin's type changes to one that is not in the specified filter,
	* or it falls outside the specified search volume, it will be marked as a "deleted" Pin even if it is still present in the environment.
	* @param InGlobalFilter Filter to use when querying pins for updates.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError SetGlobalQueryFilter(const FMagicLeapARPinQuery& InGlobalFilter);

	/**
	* The current filter used when querying pins for updates.
	* @param CurrentGlobalFilter the current filter used when querying pins for updates.
	* @return Error code representing specific success or failure cases.
	* @see SetGlobalQueryFilter()
	*/
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ContentPersistence|MagicLeap")
	static EMagicLeapPassableWorldError GetGlobalQueryFilter(FMagicLeapARPinQuery& CurrentGlobalFilter);

	/**
	 * Bind a dynamic delegate to the OnMagicLeapContentBindingFound event.
	 * 
	 * The delegate reports a PinID and the set of ObjectIds that were saved (via a MagicLeapARPinComponent) for that Pin.
	 * This delegate can be used to spawn the actors associated with that ObjectId. Spawn the actor, set the ObjectId and then call
	 * UMagicLeapARPinComponent::AttemptPinDataRestoration().
	 * @param Delegate Delegate to bind
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static void BindToOnMagicLeapContentBindingFoundDelegate(const FMagicLeapContentBindingFoundDelegate& Delegate);

	/**
	 * Unbind a dynamic delegate from the OnMagicLeapContentBindingFound event.
	 * @param Delegate Delegate to unbind
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static void UnBindToOnMagicLeapContentBindingFoundDelegate(const FMagicLeapContentBindingFoundDelegate& Delegate);

	/**
	 * Get the user index used to save / load the save game object used for storing all the content bindings (PinID and ObjectID associations in a MagicLeapARPinComponent).
	 * @return user index for the save game object
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ContentPersistence|MagicLeap")
	static int32 GetContentBindingSaveGameUserIndex();

	/**
	 * Set the user index to be used to save / load the save game object used for storing all the content bindings (PinID and ObjectID associations in a MagicLeapARPinComponent).
	 * Call this before the first tick of the level.
	 * @param UserIndex user index to be used for the save game object
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	static void SetContentBindingSaveGameUserIndex(int32 UserIndex);
};
