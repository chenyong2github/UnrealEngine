// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleManager.h"
#include "Features/IModularFeature.h"
#include "MagicLeapARPinTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Containers/Queue.h"
#include "Containers/Union.h"
#include "HAL/Event.h"
#include "Features/IModularFeatures.h"

/**
 * Magic Leap AR Pin interface
 *
 * NOTE:  This intentionally does NOT derive from IModuleInterface, to allow for a cleaner separation of code if some modular interface needs to implement ARPins as well.
 * NOTE:  You must MANUALLY call IModularFeatures::Get().RegisterModularFeature( GetModularFeatureName(), this ) in your implementation!  This allows AR Pins
 *			to be both piggy-backed off modules which support them, as well as standing alone.
 */
class MAGICLEAPARPIN_API IMagicLeapARPinFeature : public IModularFeature
{
public:
	IMagicLeapARPinFeature();
	virtual ~IMagicLeapARPinFeature();

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("MagicLeapARPinFeature"));
		return FeatureName;
	}

	static inline IMagicLeapARPinFeature* Get()
	{
		TArray<IMagicLeapARPinFeature*> ARPinImpls = IModularFeatures::Get().GetModularFeatureImplementations<IMagicLeapARPinFeature>(GetModularFeatureName());
		// return the first impl for now
		return (ARPinImpls.Num() > 0) ? ARPinImpls[0] : nullptr;
	}

	/**
	* Create an ARPin tracker.
	* @return Error code representing specific success or failure cases. If code is EMagicLeapPassableWorldError::PrivilegeRequestPending
	* or EMagicLeapPassableWorldError::StartupPending, poll for IsTrackerValid() to check when the privilege is granted and tracker successfully created.
	*/
	virtual EMagicLeapPassableWorldError CreateTracker() = 0;

	/**
	* Destroy an ARPin tracker.
	* @return Error code representing specific success or failure cases.,
	*/
	virtual EMagicLeapPassableWorldError DestroyTracker() = 0;

	/** Is an ARPin tracker already created. */
	virtual bool IsTrackerValid() const = 0;

	/**
	* Returns the count of currently available AR Pins.
	* @param Count Output param for number of currently available AR Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetNumAvailableARPins(int32& Count) = 0;

	/**
	* Returns all the AR Pins currently available.
	* @param NumRequested Max number of AR Pins to query. Pass in a negative integer to get all available Pins.
	* @param Pins Output array containing IDs of the found Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetAvailableARPins(int32 NumRequested, TArray<FGuid>& Pins) = 0;

	/**
	* Returns the Pin closest to the target point passed in.
	* @param SearchPoint Position, in world space, to search the closest Pin to.
	* @param PinID Output param for the ID of the closest Pin. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetClosestARPin(const FVector& SearchPoint, FGuid& PinID) = 0;

	/**
	* Returns filtered set of Pins based on the informed parameters.
	* @param Query Search parameters
	* @param Pins Output array containing IDs of the found Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError QueryARPins(const FMagicLeapARPinQuery& Query, TArray<FGuid>& Pins) { return EMagicLeapPassableWorldError::NotImplemented; };

	/**
	* Returns the position & orientation of the requested Pin in tracking space
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the position of the Pin in tracking space. Valid only if return value is true.
	* @param Orientation Output param for the orientation of the Pin in tracking space. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	virtual bool GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) = 0;

	/**
	* Returns the world position & orientation of the requested Pin.
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the world position of the Pin. Valid only if return value is true.
	* @param Orientation Output param for the world orientation of the Pin. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	virtual bool GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) = 0;

	/**
	* Returns the state of the requested Pin.
	* @param PinID ID of the Pin to get the state for.
	* @param State Output state of the Pin. Valid only if return value is true.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State) = 0;

	/**
	 * Delegate event to report updates in ARPins
	 * @param Added List of ARPin IDs that were added
	 * @param Updated List of ARPin IDs that were updated. Whether a pin is considered updated is determined by whehter any of its state parameters changed a specified delta.
	 * 				  The delta thresholds can be set in Project Settings > MagicLeapARPin Plugin
	 * @param Deleted List of ARPin IDs deleted
	 */
	DECLARE_EVENT_ThreeParams(IMagicLeapARPinFeature, FMagicLeapARPinUpdatedEvent, const TArray<FGuid>& /* Added */, const TArray<FGuid>& /* Updated */, const TArray<FGuid>& /* Deleted */)

	/**
	 * Getter for the OnMagicLeapARPinUpdated event, should be used to bind and unbind delegated.
	 * @return delegate event to bind to
	 */
	FMagicLeapARPinUpdatedEvent& OnMagicLeapARPinUpdated() { return OnMagicLeapARPinUpdatedEvent; }

	/**
	 * Bind a dynamic delegate to the OnMagicLeapARPinUpdated event.
	 * @param Delegate Delegate to bind
	 */
	void BindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate) { OnMagicLeapARPinUpdatedMulti.Add(Delegate); }

	/**
	 * Unbind a dynamic delegate from the OnMagicLeapARPinUpdated event.
	 * @param Delegate Delegate to unbind
	 */
	void UnBindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate) { OnMagicLeapARPinUpdatedMulti.Remove(Delegate); }

	/**
	* Set the filter used to query ARPins at the specified frequency (see UMagicLeapARPinSettings). This will alter the results reported via the OnMagicLeapARPinUpdated delegates only
	* and not the ones by GetClosestARPin() and QueryARPins().
	* By default the filter includes all available Pin in an unbounded distance. If an ARPin's type changes to one that is not in the specified filter,
	* or it falls outside the specified search volume, it will be marked as a "deleted" Pin even if it is still present in the environment.
	* @param InGlobalFilter Filter to use when querying pins for updates.
	*/
	virtual void SetGlobalQueryFilter(const FMagicLeapARPinQuery& InGlobalFilter) { GlobalFilter = InGlobalFilter; }

	/**
	* The current filter used when querying pins for updates.
	* @see SetGlobalQueryFilter()
	* @return the current filter used when querying pins for updates.
	*/
	virtual const FMagicLeapARPinQuery& GetGlobalQueryFilter() const { return GlobalFilter; }


	/**
	 * Delegate event to report the foud Content bindings (ObjectIDs of a MagicLeapARPinComponent stored in association with a PinID).
	 * @param PinId ID of the pin that was recently found in the environment.
	 * @param PinnedObjectIds Set of object ids that were saved via a MagicLeapARPinComponent.
	 */
	DECLARE_EVENT_TwoParams(IMagicLeapARPinFeature, FMagicLeapContentBindingFoundEvent, const FGuid& /* PinId */, const TSet<FString>& /* PinnedObjectIds */)

	/**
	 * Getter for the OnMagicLeapContentBindingFound event, should be used to bind and unbind delegates.
	 * @return delegate event to bind to
	 */
	FMagicLeapContentBindingFoundEvent& OnMagicLeapContentBindingFound() { return OnMagicLeapContentBindingFoundEvent; }

	/**
	 * Bind a dynamic delegate to the OnMagicLeapContentBindingFound event.
	 * 
	 * The delegate reports a PinID and the set of ObjectIds that were saved (via a MagicLeapARPinComponent) for that Pin.
	 * This delegate can be used to spawn the actors associated with that ObjectId. Spawn the actor, set the ObjectId and then call
	 * UMagicLeapARPinComponent::AttemptPinDataRestoration().
	 * @param Delegate Delegate to bind
	 */
	void BindToOnMagicLeapContentBindingFoundDelegate(const FMagicLeapContentBindingFoundDelegate& Delegate) { OnMagicLeapContentBindingFoundMulti.Add(Delegate); }

	/**
	 * Unbind a dynamic delegate from the OnMagicLeapContentBindingFound event.
	 * @param Delegate Delegate to unbind
	 */
	void UnBindToOnMagicLeapContentBindingFoundDelegate(const FMagicLeapContentBindingFoundDelegate& Delegate) { OnMagicLeapContentBindingFoundMulti.Remove(Delegate); }

	/**
	 * Get the user index used to save / load the save game object used for storing all the content bindings (PinID and ObjectID associations in a MagicLeapARPinComponent).
	 * @return user index for the save game object
	 */
	int32 GetContentBindingSaveGameUserIndex() const { return ContentBindingSaveGameUserIndex; }

	/**
	 * Set the user index to be used to save / load the save game object used for storing all the content bindings (PinID and ObjectID associations in a MagicLeapARPinComponent).
	 * Call this before the first tick of the level.
	 * @param UserIndex user index to be used for the save game object
	 */
	void SetContentBindingSaveGameUserIndex(int32 UserIndex) { ContentBindingSaveGameUserIndex = UserIndex; }

	/**
	 * Save an ObjectID associated with a given PinID. 
	 * @param PinId ID of the ARPin to associate this object id to.
	 * @param ObjectId String name to associate with the given PinID. This id, while a string so that it can be a friendly name, should be unique among all objectids saved for this app.
	 */
	void AddContentBindingAsync(const FGuid& PinId, const FString& ObjectId);

	/**
	 * Remove an ObjectID associated with a given PinID. 
	 * @param PinId ID of the ARPin associated with this object id.
	 * @param ObjectId String name associated with the given PinID
	 */
	void RemoveContentBindingAsync(const FGuid& PinId, const FString& ObjectId);

	virtual EMagicLeapPassableWorldError ARPinIdToString(const FGuid& ARPinId, FString& Str) const { return EMagicLeapPassableWorldError::NotImplemented; }

	virtual EMagicLeapPassableWorldError ParseStringToARPinId(const FString& PinIdString, FGuid& ARPinId) const { return EMagicLeapPassableWorldError::NotImplemented; }

protected:
	void BroadcastOnMagicLeapARPinUpdatedEvent(const TArray<FGuid>& Added, const TArray<FGuid>& Updated, const TArray<FGuid>& Deleted)
	{
		OnMagicLeapARPinUpdatedEvent.Broadcast(Added, Updated, Deleted);
	}

	FMagicLeapARPinUpdatedEvent OnMagicLeapARPinUpdatedEvent;
	FMagicLeapARPinUpdatedMultiDelegate OnMagicLeapARPinUpdatedMulti;

	FMagicLeapARPinQuery GlobalFilter;

private:
	void OnARPinsUpdated(const TArray<FGuid>& Added, const TArray<FGuid>& Updated, const TArray<FGuid>& Deleted);
	void OnSaveGameToSlot(const FString& InSlotName, const int32 InUserIndex, bool bDataSaved);
	void OnLoadGameFromSlot(const FString& InSlotName, const int32 InUserIndex, USaveGame* InSaveGameObj);
	void LoadBindingsFromDiskAsync(bool bCreateIfNeeded = false);
	void SaveBindingsToDiskAsync();

	void BroadcastOnMagicLeapContentBindingFoundEvent(const FGuid& PinId, const TSet<FString>& PinnedObjectIds)
	{
		OnMagicLeapContentBindingFoundEvent.Broadcast(PinId, PinnedObjectIds);
	}

	FMagicLeapContentBindingFoundEvent OnMagicLeapContentBindingFoundEvent;
	FMagicLeapContentBindingFoundMultiDelegate OnMagicLeapContentBindingFoundMulti;

	UMagicLeapARPinContentBindings* ContentBindingSave;
	int32 ContentBindingSaveGameUserIndex;

	FAsyncSaveGameToSlotDelegate SaveGameDelegate;
	FAsyncLoadGameFromSlotDelegate LoadGameDelegate;

	enum class EQueueTaskType : uint8
	{
		Add,
		Remove,
		Search
	};

	void QueueContentBindingOperation(enum EQueueTaskType TaskType, const FGuid& PinId, const FString& ObjectId);

	struct FSaveGameDataCache
	{
		FGuid PinId;
		FString ObjectId;
	};

	struct FARPinDataCache
	{
		TArray<FGuid> Added;
	};

	struct FQueueData
	{
		EQueueTaskType Type;
		TUnion<FSaveGameDataCache, FARPinDataCache> Data;
	};

	// only used for operations performed while the SaveGameObject was being loaded.
	TQueue<FQueueData, EQueueMode::Spsc> PendingTasks;

	// For synchronizing SaveGameData operations once the data is already loaded.
	bool bSaveGameDataIsDirty;
	bool bSaveGameInProgress;

	bool bLoadGameInProgress;

	static const FString ContentBindingSaveGameSlotName;
};

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapARPin, Verbose, All);
