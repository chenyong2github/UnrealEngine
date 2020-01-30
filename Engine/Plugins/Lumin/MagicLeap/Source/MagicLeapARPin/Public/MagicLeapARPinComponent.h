// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Components/SceneComponent.h"
#include "MagicLeapARPinTypes.h"
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
	 * Pin given SceneComponent to the closest AR Pin in real-world.
	 * OnPersistentEntityPinned event will be fired when a suitable AR Pin is found for this component.
	 * The component's transform will then be locked. App needs to call UnPin() if it wants to move the component again.
	 * @param ComponentToPin SceneComponent to pin to the world. Pass in 'this' component if app is using 'OnlyOnDataRestoration' or 'Always' AutoPinType.
	 * @return true if the component was accepted to be pinned, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool PinSceneComponent(USceneComponent* ComponentToPin);

	/**
	 * Pin given Actor to the closest AR Pin in real-world.
	 * OnPersistentEntityPinned event will be fired when a suitable AR Pin is found for this Actor.
	 * The Actor's transform will then be locked. App needs to call UnPin() if it wants to move the Actor again.
	 * @param ActorToPin Actor to pin to the world. Pass in this component's owner if app is using 'OnlyOnDataRestoration' or 'Always' AutoPinType.
	 * @return true if the Actor was accepted to be pinned, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap")
	bool PinActor(AActor* ActorToPin);

	/**
	 * Detach or un-pin the currently pinned entity (component or actor) from the real-world.
	 * Call this if you want to change the transform of a pinned entity.
	 * Note that if you still want your content to persist, you will have to call PinSceneComponent() or PinActor() before EndPlay().
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
	bool GetPinnedPinID(FGuid& PinID);

	/**
	 * Retrieves the data associated with this pin.
	 * @param PinDataClass The user defined save game class used by this pin.  Note that this must match the PinDataClass property.
	 * @return The save game instance associated with this pin instance.
	 */
	UFUNCTION(BlueprintCallable, Category = "ContentPersistence|MagicLeap", meta = (DeterminesOutputType = "PinDataClass"))
	UMagicLeapARPinSaveGame* GetPinData(TSubclassOf<UMagicLeapARPinSaveGame> PinDataClass);

public:
	/**
	 * Unique ID for this component to save the meta data for the Pin and make content persistent.
	 * This name has to be unique across all instances of the MagicLeapARPinComponent class.
	 * If empty, the name of the owner actor will be used.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
	FString ObjectUID;

	/** Index to get the save game data for the pin */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ContentPersistence|MagicLeap")
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

	bool bPinned;
	bool bDataRestored;
	bool bAttemptedPinningAfterTrackerCreation;
	bool bPinFoundInEnvironmentPrevFrame;

	class FMagicLeapARPinTrackerImpl *Impl;
};
