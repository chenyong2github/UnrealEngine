// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/SceneComponent.h"
#include "MagicLeapARPinTypes.h"
#include "Kismet/GameplayStatics.h"
#include "MagicLeapARPinComponent.generated.h"

class AActor;

/** Component to make content persist at locations in the real world. */
UCLASS(ClassGroup = MagicLeap, BlueprintType, Blueprintable, EditInlineNew, meta = (BlueprintSpawnableComponent))
class MAGICLEAPARPIN_API UMagicLeapARPinComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMagicLeapARPinComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Pin this component (or owner actor if bShouldPinActor is true) to the specified PinID.
	 * If this pin exists in the environment, OnPersistentEntityPinned event will be fired in the next Tick.
	 * The component's transform will then be locked. App needs to call UnPin() if it wants to move the component again.
	 * @param PinID ID of the ARPin to attach this component (or owner actor) to.
	 * @return true if the provided pin exists in the environment, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool PinToID(const FGuid& PinID);

	/**
	 * Pin this component (or owner actor if bShouldPinActor is true) to the ARPin that is the best fit based on its location and
	 * desired type. If UMagicLeapARPinFunctionLibrary::QueryARPins() is implemented on this platform, pins of type SearchPinTypes
	 * will be searched within the SearchVolume (250cm radius by default) and the closest one will be selected. Otherwise, a simple
	 * search of the closest pin will be used to get the desired ARPin.
	 * OnPersistentEntityPinned event will be fired when a suitable ARPin is found for this component.
	 * The component's transform will then be locked. App needs to call UnPin() if it wants to move the component again.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	void PinToBestFit();

	/**
	 * Pin this component (or owner actor if bShouldPinActor is true) to the PinID that was restored from a previous session
	 * or was synced voer the network.
	 * OnPersistentEntityPinned event will be fired when the restored pin will be found in the environment.
	 * The component's transform will then be locked. App needs to call UnPin() if it wants to move the component again.
	 * @return true if some ARPin data has been restored or synced, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool PinToRestoredOrSyncedID();

	/**
	 * Deprecated and can no longer be used to pin any scene component other than itself. Use PinToBestFit(), PinToID() or PinToRestoredOrSyncedID() instead.
	 * If a reference to itself passed to this function, PinToBestFit() is called internally.
	 * @param ComponentToPin Only accepts 'this' / 'self'
	 * @return true if the component was accepted to be pinned, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap", meta = (DeprecatedFunction, DeprecationMessage="Will be removed in MLSDK 0.25.0. Can pin only itself (will pin its children with it). Use PinToBestFit(), PinToID() or PinToRestoredOrSyncedID()."))
	bool PinSceneComponent(USceneComponent* ComponentToPin);

	/**
	 * Deprecated and can no longer be used to pin any actor other than the owner of this component.
	 * Set bShouldPinActor to true and call PinToBestFit(), PinToID() or PinToRestoredOrSyncedID() instead.
	 * If this component's owner is passed to this function, PinToBestFit() is called internally.
	 * @param ActorToPin Only accepts the owner of this component
	 * @return true if the Actor was accepted to be pinned, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap", meta = (DeprecatedFunction, DeprecationMessage="Will be removed in MLSDK 0.25.0. Set bShouldPinActor to true and call PinToBestFit(), PinToID() or PinToRestoredOrSyncedID()."))
	bool PinActor(AActor* ActorToPin);

	/**
	 * Detach or un-pin the currently pinned entity (component) from the real-world.
	 * Call this if you want to change the transform of a pinned entity.
	 * Note that if you still want your content to persist, you will have to call PinToBestFit() or PinToID() before EndPlay().
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	void UnPin();

	/**
	 * True if an entity (component or actor) is currently pinned by this component.
	 * If true, the entity's transform will be locked. App needs to call UnPin() if it wants to move it again.
	 * If false, and you still want your content to persist, you will have to call PinSceneComponent() or PinActor() before EndPlay().
	 * @return True if an entity (component or actor) is currently pinned by this component.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool IsPinned() const;

	/**
	 * True if the AR Pin for the unique ID ObjectUID was restored from the app's local storage or was repliated over network.
	 * Implies if content was already pinned earlier. Does not imply if that restored Pin is available in the current environment.
	 * @return True if the Pin data was restored from local storage or network.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool PinRestoredOrSynced() const;

	/**
	 * Get the ID of the Pin the entity (component or actor) is currently pinned to.
	 * @param PinID Output param for the ID of the Pin.
	 * @return True if an entity is currently pinned by this component and the output param is successfully populated.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool GetPinnedPinID(FGuid& PinID) const;

	/**
	 * Retrieves the data associated with this pin. Make sure to call this only after setting a proper ObjectID value.
	 * @param PinDataClass The user defined save game class used by this pin.  Note that this must match the PinDataClass property.
	 * @return The save game instance associated with this pin instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap", meta = (DeterminesOutputType = "PinDataClass", DeprecatedFunction, DeprecationMessage="Deprecated and will be removed in 0.24.2 release. Use TryGetPinData() instead."))
	UMagicLeapARPinSaveGame* GetPinData(TSubclassOf<UMagicLeapARPinSaveGame> PinDataClass);

	/**
	 * Tries to retreive the data associated with this pin. Returns false if the data hasnt been loaded from the disk yet.
	 * In that case, wait until the OnPinDataLoadAttemptCompleted event is called.
	 * @param InPinDataClass The user defined save game class used by this pin. Note that this must match the PinDataClass property. Used for auto casting the return value.
	 * @param OutPinData The save game instance associated with this pin instance, only valid if function returns true.
	 * @return True if pin data was successfully loaded, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap", meta = (DeterminesOutputType = "InPinDataClass"))
	UMagicLeapARPinSaveGame* TryGetPinData(TSubclassOf<UMagicLeapARPinSaveGame> InPinDataClass, bool& OutPinDataValid);

	/**
	* Returns the state of this Pin.
	* @param State Output state of the Pin. Valid only if return value is true.
	* @return Error code representing specific success or failure cases.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool GetPinState(FMagicLeapARPinState& State) const;

	/**
	* If BeginPlay() is called before app sets ObjectUID (can happen when
	* component is spawned at runtime or actor that includes this component
	* is spawned at runtime), this function can be called to attempt a fresh
	* restoration.
	* @return true of pin data was retored, false otherwise
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap", meta=(DeprecatedFunction, DeprecationMessage="Deprecated and will be removed in 0.24.2 release. Use AttemptPinDataRestorationAsync instead."))
	bool AttemptPinDataRestoration();

	/**
	* If BeginPlay() is called before app sets ObjectUID (can happen when
	* component is spawned at runtime or actor that includes this component
	* is spawned at runtime), this function can be called to attempt a fresh
	* restoration. It loads the pin data from disk asynchronously. 
	* The OnPinDataLoadAttemptCompleted event is called to indicate whether 
	* the data was succesfully loaded or not.
	*/
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	void AttemptPinDataRestorationAsync();

public:
	/**
	 * Unique ID for this component to save the meta data for the Pin and make content persistent.
	 * This name has to be unique across all instances of the MagicLeapARPinComponent class.
	 * If empty, the name of the owner actor will be used.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap", meta = (ExposeOnSpawn = true))
	FString ObjectUID;

	/** Index to get the save game data for the pin */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap", meta = (ExposeOnSpawn = true))
	int32 UserIndex;

	/** Mode for automatically pinning this component or it's owner actor to real-world. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
	EMagicLeapAutoPinType AutoPinType;

	/** Pin this component's owner actor instead of just the component itself. Relevant only when using 'OnlyOnDataRestoration' or 'Always' as AutoPinType. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
	bool bShouldPinActor;

	/** The user defined save game class associated with this pin.  Note that this MUST match the type passed into GetPinData().*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
	TSubclassOf<UMagicLeapARPinSaveGame> PinDataClass;

	/** Pin types to look for when attempting to pin this component. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
	TSet<EMagicLeapARPinType> SearchPinTypes;

	/** Volume to search for an ARPin in. The position and scaled radius (in Unreal Units) of this sphere is used to look for an ARPin of type SearchPinTypes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
	class USphereComponent* SearchVolume;

	/**
	 * Delegate used to notify the instigating blueprint that an entity (component or actor) has been successfully pinned to the real-world.
	 * Indicates that the transform of the pinned entity is now locked. App needs to call UnPin() if it wants to move the entity again.
	 * @param bRestoredOrSynced True if the entity was pinned as a result of Pin data being restored from local storage or replicatred over network, false if pinned by an explicit PinSceneComponent() or PinActor() call from the app.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPersistentEntityPinned, bool, bRestoredOrSynced);

	/** Fired when an entity is successfully pinned by this component. */
	UPROPERTY(BlueprintAssignable)
	FPersistentEntityPinned OnPersistentEntityPinned;

	/**
	* Delegate used to notify the instigating blueprint that an entity (component or actor) has lost a previously obtained pin.
	*/
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPersistentEntityPinLost);

	/** Fired when an entity loses its pin. */
	UPROPERTY(BlueprintAssignable)
	FPersistentEntityPinLost OnPersistentEntityPinLost;

	/**
	 * Delegate used to notify that the pin data associated with this component's ObjectUID has been successfully loaded or not.
	 * @param bDataRestored True if the pin data was loaded, false otherwise.
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMagicLeapARPinDataLoadAttemptCompleted, bool, bDataRestored);

	UPROPERTY(BlueprintAssignable)
	FMagicLeapARPinDataLoadAttemptCompleted OnPinDataLoadAttemptCompleted;

private:
	void SelectComponentToPin();
	void SavePinData(const FGuid& InPinID, const FTransform& InComponentToWorld, const FTransform& InPinTransform, bool bRegisterDelegate);
	EMagicLeapPassableWorldError FindBestFitPin(class IMagicLeapARPinFeature* ARPinImpl, FGuid& FoundPin);
	bool TryInitOldTransformData();
	void OnSaveGameToSlot(const FString& InSlotName, const int32 InUserIndex, bool bDataSaved);
	void OnLoadGameFromSlot(const FString& InSlotName, const int32 InUserIndex, USaveGame* InSaveGameObj);

private:
	UPROPERTY()
	FGuid PinnedCFUID;

	UPROPERTY()
	USceneComponent* PinnedSceneComponent;

	UPROPERTY()
	UMagicLeapARPinSaveGame* PinData;

	FTransform OldComponentWorldTransform;
	FTransform OldCFUIDTransform;
	FTransform NewComponentWorldTransform;
	FTransform NewCFUIDTransform;

	bool bHasValidPin;
	bool bDataRestored;
	bool bAttemptedPinningAfterDataRestoration;
	bool bPinFoundInEnvironmentPrevFrame;

	FAsyncSaveGameToSlotDelegate SaveGameDelegate;
	FAsyncLoadGameFromSlotDelegate LoadGameDelegate;
};
