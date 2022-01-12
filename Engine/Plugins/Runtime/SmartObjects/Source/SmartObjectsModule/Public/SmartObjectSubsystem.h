// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectCollection.h"
#include "Templates/SubclassOf.h"
#include "SmartObjectOctree.h"
#include "SmartObjectTypes.h"
#include "SmartObjectRuntime.h"
#include "Subsystems/WorldSubsystem.h"
#include "SmartObjectSubsystem.generated.h"

class USmartObjectComponent;

#if WITH_EDITOR
/** Called when main collection changed. */
DECLARE_MULTICAST_DELEGATE(FOnMainCollectionChanged);
#endif

/**
 * Struct that can be used to filter results of a smart object request when trying to find or claim a smart object
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectRequestFilter
{
	GENERATED_BODY()

	FSmartObjectRequestFilter(const FGameplayTagContainer& InUserTags, const FGameplayTagQuery& InRequirements)
		: UserTags(InUserTags)
		, ActivityRequirements(InRequirements)
	{}

	explicit FSmartObjectRequestFilter(const FGameplayTagContainer& InUserTags)
		: UserTags(InUserTags)
	{}

	explicit FSmartObjectRequestFilter(const FGameplayTagQuery& InRequirements)
		: ActivityRequirements(InRequirements)
	{}

	explicit FSmartObjectRequestFilter(const TSubclassOf<USmartObjectBehaviorDefinition> DefinitionClass)
		: BehaviorDefinitionClass(DefinitionClass)
	{}

	FSmartObjectRequestFilter() = default;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SmartObject)
	FGameplayTagContainer UserTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SmartObject)
	FGameplayTagQuery ActivityRequirements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SmartObject)
	TSubclassOf<USmartObjectBehaviorDefinition> BehaviorDefinitionClass;

	TFunction<bool(FSmartObjectID)> Predicate;
};

/**
 * Struct used to find a smart object within a specific search range and with optional filtering
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectRequest
{
	GENERATED_BODY()

	FSmartObjectRequest() = default;
	FSmartObjectRequest(const FBox& InQueryBox, const FSmartObjectRequestFilter& InFilter)
		: QueryBox(InQueryBox)
		, Filter(InFilter)
	{}

	/** Box defining the search range */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SmartObject)
	FBox QueryBox = FBox(ForceInitToZero);

	/** Struct used to filter out some results (all results allowed by default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = SmartObject)
	FSmartObjectRequestFilter Filter;
};

/**
 * Struct that holds the object and slot selected by processing a smart object request.
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectRequestResult
{
	GENERATED_BODY()

	FSmartObjectRequestResult(const FSmartObjectID& InSmartObjectID, const FSmartObjectSlotIndex& InSlotIndex)
		: SmartObjectID(InSmartObjectID)
		, SlotIndex(InSlotIndex)
	{}

	FSmartObjectRequestResult() = default;

	bool IsValid() const { return SmartObjectID.IsValid() && SlotIndex.IsValid(); }

	bool operator==(const FSmartObjectRequestResult& Other) const
	{
		return IsValid() && Other.IsValid()
			&& SmartObjectID == Other.SmartObjectID
			&& SlotIndex == Other.SlotIndex;
	}

	bool operator!=(const FSmartObjectRequestResult& Other) const
	{
		return !(*this == Other);
	}
	
	FString Describe() const
	{
		return FString::Printf(TEXT("Object:%s Use:%s"), *SmartObjectID.Describe(), *SlotIndex.Describe());
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = SmartObject)
	FSmartObjectID SmartObjectID;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = SmartObject)
	FSmartObjectSlotIndex SlotIndex;
};

/**
 * Result code indicating if the Collection was successfully registered or why it was not.
 */
UENUM()
enum class ESmartObjectCollectionRegistrationResult
{
	Failed_InvalidCollection,
	Failed_AlreadyRegistered,
	Failed_NotFromPersistentLevel,
	Succeeded,
};

/**
 * Subsystem that holds all registered smart object instances and offers the API for spatial queries and reservations.
 */
UCLASS(config = Game)
class SMARTOBJECTSMODULE_API USmartObjectSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	USmartObjectSubsystem();

	static USmartObjectSubsystem* GetCurrent(const UWorld* World);

	ESmartObjectCollectionRegistrationResult RegisterCollection(ASmartObjectCollection& InCollection);
	void UnregisterCollection(ASmartObjectCollection& InCollection);
	ASmartObjectCollection* GetMainCollection() const { return MainCollection; }

	bool RegisterSmartObject(USmartObjectComponent& SmartObjectComponent);
	bool UnregisterSmartObject(USmartObjectComponent& SmartObjectComponent);

	/**
	 * Returns the component associated to the claim handle if still
	 * accessible. In some scenarios the component may no longer exist
	 * but its smart object data could (e.g. streaming)
	 * @param ClaimHandle Valid handle to a claimed smart object slot
	 * @return A pointer to the USmartObjectComponent* associated to the handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	USmartObjectComponent* GetSmartObjectComponent(const FSmartObjectClaimHandle& ClaimHandle) const;

	bool RegisterSmartObjectActor(const AActor& SmartObjectActor);
	bool UnregisterSmartObjectActor(const AActor& SmartObjectActor);

	/**
	 * Spatial lookup
	 * @return First valid smart object in range. Not the closest one, just the one
	 *		that happens to be retrieved first from the octree
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	FSmartObjectRequestResult FindSmartObject(const FSmartObjectRequest& Request);

	/**
	 * Spatial lookup
	 * @return All valid smart objects in range.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool FindSmartObjects(const FSmartObjectRequest& Request, TArray<FSmartObjectRequestResult>& OutResults);

	/**
	 * Goes through all defined slots of a given smart object and finds the first one matching the filter.
	 * @return Identifier of a valid slot to use. Call IsValid on it to check if the search was successful.
	 */
	UE_NODISCARD FSmartObjectRequestResult FindSlot(const FSmartObjectID ID, const FSmartObjectRequestFilter& Filter) const;

	/**
	 *	Claim smart object from a valid request result.
	 *	@param RequestResult Valid request result for given smart object and slot index. Ensure when called with an invalid result.
	 *	@return A claim handle binding the claimed smart object, its use index and a user id.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	FSmartObjectClaimHandle Claim(const FSmartObjectRequestResult& RequestResult);

	UE_NODISCARD FSmartObjectClaimHandle Claim(FSmartObjectID ID, const FSmartObjectRequestFilter& Filter = {});

	/**
	 *	Start using a claimed smart object slot.
	 *	@param ClaimHandle Handle for given pair of user and smart object. Error will be reported if the handle is invalid.
	 *	@param DefinitionClass The type of behavior definition the user wants to use.
	 *	@return The base class pointer of the requested behavior definition class associated to the slot
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	const USmartObjectBehaviorDefinition* Use(const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass);

	/**
	 *	Start using a claimed smart object slot.
	 *	@param ClaimHandle Handle for given pair of user and smart object. Error will be reported if the handle is invalid.
	 *	@return The requested behavior definition class pointer associated to the slot
	 */
	template <typename DefinitionType>
	const DefinitionType* Use(const FSmartObjectClaimHandle& ClaimHandle)
	{
		static_assert(TIsDerivedFrom<DefinitionType, USmartObjectBehaviorDefinition>::IsDerived, "DefinitionType must derive from USmartObjectBehaviorDefinition");
		return Cast<const DefinitionType>(Use(ClaimHandle, DefinitionType::StaticClass()));
	}

	/**
	 *	Release claim on a smart object.
	 *	@param ClaimHandle Handle for given pair of user and smart object. Does nothing if the handle is invalid.
	 *	@return Whether the claim was successfully released or not
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool Release(const FSmartObjectClaimHandle& ClaimHandle);

	/**
	 * Returns the position (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by ClaimUse or ClaimSmartObject.
	 * @return Position (in world space) of the slot associated to ClaimHandle.
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	TOptional<FVector> GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle) const;

	/**
	 * Returns the position (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by ClaimUse or ClaimSmartObject.
	 * @param OutSlotLocation Position (in world space) of the slot associated to ClaimHandle.
	 * @return Whether the location was found and assigned to 'OutSlotLocation'
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle, FVector& OutSlotLocation) const;

	/**
	 * Returns the position (in world space) of the slot associated to the given request result.
	 * @param Result A valid request result (Result.IsValid() returns true) returned by FindValidUse or FindSmartObject.
	 * @return Position (in world space) of the slot associated to ClaimHandle.
	 * @note Method will ensure on invalid FSmartObjectRequestResult.
	 */
	TOptional<FVector> GetSlotLocation(const FSmartObjectRequestResult& Result) const;

	/**
	 * Returns the position (in world space) of the slot represented by the provided object id and slot index.
	 * @param SmartObjectID Identifier of the smart object.
	 * @param SlotIndex Index within the list of available slots in the smart object represented by SmartObjectID.
	 * @return Position (in world space) of the slot represented by SmartObjectID and SlotIndex.
	 * @note Method will ensure on invalid FSmartObjectID or an invalid index.
	 */
	TOptional<FVector> GetSlotLocation(const FSmartObjectID SmartObjectID, const FSmartObjectSlotIndex SlotIndex) const;

	/**
	 * Returns the transform (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by ClaimUse or ClaimSmartObject.
	 * @return Transform (in world space) of the slot associated to ClaimHandle.
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	TOptional<FTransform> GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle) const;

	/**
	 * Returns the transform (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by ClaimUse or ClaimSmartObject.
	 * @param OutSlotTransform Transform (in world space) of the slot associated to ClaimHandle.
	 * @return Whether the transform was found and assigned to 'OutSlotTransform'
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle, FTransform& OutSlotTransform) const;
	
	/**
	 * Returns the transform (in world space) of the slot associated to the given request result.
	 * @param Result A valid request result (Result.IsValid() returns true) returned by FindValidUse or FindSmartObject.
	 * @return Transform (in world space) of the slot associated to ClaimHandle.
	 * @note Method will ensure on invalid FSmartObjectRequestResult.
	 */
	TOptional<FTransform> GetSlotTransform(const FSmartObjectRequestResult& Result) const;

	/**
	 * Returns the transform (in world space) of the slot represented by the provided object id and slot index.
	 * @param SmartObjectID Identifier of the smart object.
	 * @param SlotIndex Index within the list of available slots in the smart object represented by SmartObjectID.
	 * @return Transform (in world space) of the slot represented by SmartObjectID and SlotIndex.
	 * @note Method will ensure on invalid FSmartObjectID or an invalid index.
	 */
	TOptional<FTransform> GetSlotTransform(const FSmartObjectID SmartObjectID, const FSmartObjectSlotIndex SlotIndex) const;

	/** @return The octree used by the subsystem to store all registered smart objects. */
	const FSmartObjectOctree& GetOctree() const;

	/**
	 *	Register a callback to be notified if the claimed slot is no longer available and user need to perform cleanup.
	 *	@param ClaimHandle Handle to identify the object and slot. Error will be reported if the handle is invalid.
	 *	@param Callback Delegate that will be called to notify that a slot gets invalidated and can no longer be used.
	 */
	void RegisterSlotInvalidationCallback(const FSmartObjectClaimHandle& ClaimHandle, const FOnSlotInvalidated& Callback);

	/**
	 *	Unregisters a callback to be notified if the claimed slot is no longer available and user need to perform cleanup.
	 *	@param ClaimHandle Handle to identify the object and slot. Error will be reported if the handle is invalid.
	 */
	void UnregisterSlotInvalidationCallback(const FSmartObjectClaimHandle& ClaimHandle);

#if WITH_EDITOR
	mutable FOnMainCollectionChanged OnMainCollectionChanged;
#endif

protected:

	/**
	 * Callback overriden to gather loaded collections, spawn missing one and set the main collection.
	 * @note we use this method instead of `Initialize` or `PostInitialize` so active level is set and actors registered.
	 */
	virtual void OnWorldComponentsUpdated(UWorld& World) override;

	/**
	 * BeginPlay will push all objects stored in the collection to the runtime simulation
	 * and initialize octree using collection bounds.
	 */
	virtual void OnWorldBeginPlay(UWorld& World) override;

	/**
	 * Goes through all defined slots of smart object represented by SmartObjectRuntime
	 * and finds the first one given actor can use.
	 * @return identifier indicating valid slot to use. Call IsValid on it to check if the search was successful.
	 */
	FSmartObjectSlotIndex FindSlot(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectRequestFilter& Filter) const;

	const USmartObjectBehaviorDefinition* Use(FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass);

	void AbortAll(FSmartObjectRuntime& SmartObjectRuntime);

	FSmartObjectSlotRuntimeData* GetMutableRuntimeSlot(const FSmartObjectClaimHandle& ClaimHandle);

	/** Make sure that all SmartObjectCollection actors from our associated world are registered. */
	void RegisterCollectionInstances();

	void AddToSimulation(const FSmartObjectID ID, const USmartObjectDefinition& Definition, const FTransform& Transform, const FBox& Bounds);
	void AddToSimulation(const FSmartObjectCollectionEntry& Entry, const USmartObjectDefinition& Definition);
	void AddToSimulation(const USmartObjectComponent&);
	void RemoveFromSimulation(const FSmartObjectID ID);
	void RemoveFromSimulation(const FSmartObjectCollectionEntry& Entry);
	void RemoveFromSimulation(const USmartObjectComponent& SmartObjectComponent);

protected:
	UPROPERTY()
	ASmartObjectCollection* MainCollection;

	FSmartObjectOctree SmartObjectOctree;

	TMap<FSmartObjectID, FSmartObjectRuntime> RuntimeSmartObjects;

	/** Keep track of Ids associated to objects entirely created at runtime (i.e. not part of the initial collection) */
	TArray<FSmartObjectID> RuntimeCreatedEntries;

	UE::SmartObject::ID NextFreeUserID;

	/** Flag to indicate that all entries from the baked collection are registered and new registrations will be considered runtime entries (i.e. no persistence) */
	bool bInitialCollectionAddedToSimulation = false;

#if WITH_EDITOR
	friend class ASmartObjectCollection;
	void RebuildCollection(ASmartObjectCollection& InCollection);
	void SpawnMissingCollection();

	/**
	 * Compute bounds from given world and store result in provided collection
	 * @param World World from which the bounds must be computed
	 * @param Collection Collection that will store computed bounds
	 */
	void ComputeBounds(const UWorld& World, ASmartObjectCollection& Collection) const;
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	/** List of registered used to rebuild collection on demand */
	UPROPERTY(Transient)
	TArray<USmartObjectComponent*> RegisteredSOComponents;
#endif // WITH_EDITORONLY_DATA

#if WITH_SMARTOBJECT_DEBUG
public:
	uint32 DebugGetNumRuntimeObjects() const { return RuntimeSmartObjects.Num(); }
	const TMap<FSmartObjectID, FSmartObjectRuntime>& DebugGetRuntimeObjects() const { return RuntimeSmartObjects; }
	uint32 DebugGetNumRegisteredComponents() const { return DebugRegisteredComponents.Num(); }

	/** Debugging helper to remove all registered smart objects from the simulation */
	void DebugUnregisterAllSmartObjects();

	/** Debugging helpers to add all registered smart objects to the simulation */
	void DebugRegisterAllSmartObjects();

private:
	TArray<TWeakObjectPtr<USmartObjectComponent>> DebugRegisteredComponents;
#endif // WITH_SMARTOBJECT_DEBUG
};
