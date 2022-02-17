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
class UMassEntitySubsystem;

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

	TFunction<bool(FSmartObjectHandle)> Predicate;
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

	FSmartObjectRequestResult(const FSmartObjectHandle& InSmartObjectHandle, const FSmartObjectSlotHandle InSlotHandle = {})
		: SmartObjectHandle(InSmartObjectHandle)
		, SlotHandle(InSlotHandle)
	{}

	FSmartObjectRequestResult() = default;

	bool IsValid() const { return SmartObjectHandle.IsValid() && SlotHandle.IsValid(); }

	bool operator==(const FSmartObjectRequestResult& Other) const
	{
		return IsValid() && Other.IsValid()
			&& SmartObjectHandle == Other.SmartObjectHandle
			&& SlotHandle == Other.SlotHandle;
	}

	bool operator!=(const FSmartObjectRequestResult& Other) const
	{
		return !(*this == Other);
	}
	
	friend FString LexToString(const FSmartObjectRequestResult& Result)
	{
		return FString::Printf(TEXT("Object:%s Slot:%s"), *LexToString(Result.SmartObjectHandle), *LexToString(Result.SlotHandle));
	}

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = SmartObject)
	FSmartObjectHandle SmartObjectHandle;

	UPROPERTY(Transient, VisibleAnywhere, Category = SmartObject)
	FSmartObjectSlotHandle SlotHandle;
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
 * Indicates how extensive a search for slots should be within a single SmartObject.
 */
UENUM()
enum class ESmartObjectSlotSearchMode
{
	FirstMatch,
	AllMatches
};

/**
 * Subsystem that holds all registered smart object instances and offers the API for spatial queries and reservations.
 */
UCLASS(config = Game)
class SMARTOBJECTSMODULE_API USmartObjectSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
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
	FSmartObjectRequestResult FindSmartObject(const FSmartObjectRequest& Request) const;

	/**
	 * Spatial lookup
	 * @return All valid smart objects in range.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool FindSmartObjects(const FSmartObjectRequest& Request, TArray<FSmartObjectRequestResult>& OutResults) const;

	/**
	 * Goes through all defined slots of a given smart object and finds the first one matching the filter.
	 * @return Identifier of a valid slot to use. Call IsValid on it to check if the search was successful.
	 */
	UE_NODISCARD FSmartObjectRequestResult FindSlot(const FSmartObjectHandle Handle, const FSmartObjectRequestFilter& Filter) const;

	/**
	 * Returns slots of a given smart object matching the filter.
	 * @param Handle Handle to the SmartObject
	 * @param Filter Filter to apply on object and slots
	 * @param OutSlots Available slots found that match the filter
	 * @param SearchMode Indicates if the result must include all matching slots or only the first one matching
	 */
	void FindSlots(const FSmartObjectHandle Handle,
				   const FSmartObjectRequestFilter& Filter,
				   TArray<FSmartObjectSlotHandle>& OutSlots,
				   ESmartObjectSlotSearchMode SearchMode = ESmartObjectSlotSearchMode::AllMatches) const;

	/**
	 *	Claim smart object from a valid request result.
	 *	@param RequestResult Valid request result for given smart object and slot index. Ensure when called with an invalid result.
	 *	@return A claim handle binding the claimed smart object, its use index and a user id.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	FSmartObjectClaimHandle Claim(const FSmartObjectRequestResult& RequestResult);

	UE_NODISCARD FSmartObjectClaimHandle Claim(FSmartObjectHandle Handle, const FSmartObjectRequestFilter& Filter = {});

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
	 *	Return the behavior definition of a given type from a claimed object.
	 *	@param ClaimHandle Handle for given pair of user and smart object. Error will be reported if the handle is invalid.
	 *	@param DefinitionClass The type of behavior definition.
	 *	@return The base class pointer of the requested behavior definition class associated to the slotClaim handle
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	const USmartObjectBehaviorDefinition* GetBehaviorDefinition(const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass);

	/**
	 *	Return the behavior definition of a given type from a claimed object.
	 *	@param ClaimHandle Handle for given pair of user and smart object. Error will be reported if the handle is invalid.
	 *	@return The requested behavior definition class pointer associated to the Claim handle
	 */
	template <typename DefinitionType>
	const DefinitionType* GetBehaviorDefinition(const FSmartObjectClaimHandle& ClaimHandle)
	{
		static_assert(TIsDerivedFrom<DefinitionType, USmartObjectBehaviorDefinition>::IsDerived, "DefinitionType must derive from USmartObjectBehaviorDefinition");
		return Cast<const DefinitionType>(GetBehaviorDefinition(ClaimHandle, DefinitionType::StaticClass()));
	}

	ESmartObjectSlotState GetSlotState(FSmartObjectSlotHandle SlotHandle) const;

	/**
	 * Adds state data (through a deferred command) to a slot instance. Data must be a struct that inherits
	 * from FSmartObjectSlotStateData and passed as a struct view (e.g. FConstStructView::Make(FSomeStruct))
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by any of the Claim methods.
	 * @param InData A view on the struct to add
	 */
	void AddSlotDataDeferred(const FSmartObjectClaimHandle& ClaimHandle, FConstStructView InData) const;

	/** Returns a view to the data associated to a valid slot handle (SlotHandle.IsValid() returns true) */
	FSmartObjectSlotView GetSlotView(const FSmartObjectSlotHandle& SlotHandle) const;

	/** Returns a view to the data associated to a valid request result (Result.IsValid() returns true) */
	FSmartObjectSlotView GetSlotView(const FSmartObjectRequestResult& Result) const;

	/** Returns a view to the data associated to a valid claim handle (ClaimHandle.IsValid() returns true) */
	FSmartObjectSlotView GetSlotView(const FSmartObjectClaimHandle& ClaimHandle) const;

	/**
	 * Returns the position (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by any of the Claim methods.
	 * @return Position (in world space) of the slot associated to ClaimHandle.
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	TOptional<FVector> GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle) const;

	/**
	 * Returns the position (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by any of the Claim methods.
	 * @param OutSlotLocation Position (in world space) of the slot associated to ClaimHandle.
	 * @return Whether the location was found and assigned to 'OutSlotLocation'
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool GetSlotLocation(const FSmartObjectClaimHandle& ClaimHandle, FVector& OutSlotLocation) const;

	/**
	 * Returns the position (in world space) of the slot associated to the given request result.
	 * @param Result A valid request result (Result.IsValid() returns true) returned by any of the Find methods.
	 * @return Position (in world space) of the slot associated to Result.
	 * @note Method will ensure on invalid FSmartObjectRequestResult.
	 */
	TOptional<FVector> GetSlotLocation(const FSmartObjectRequestResult& Result) const;

	/**
	 * Returns the position (in world space) of the slot represented by the provided slot handle.
	 * @param SlotHandle Handle to a smart object slot.
	 * @return Position (in world space) of the slot associated to SlotHandle.
	 * @note Method will ensure on invalid slot handle.
	 */
	TOptional<FVector> GetSlotLocation(FSmartObjectSlotHandle SlotHandle) const;

	/**
	 * Returns the transform (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by any of the Claim methods.
	 * @return Transform (in world space) of the slot associated to ClaimHandle.
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	TOptional<FTransform> GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle) const;

	/**
	 * Returns the transform (in world space) of the slot associated to the given claim handle.
	 * @param ClaimHandle A valid handle (ClaimHandle.IsValid() returns true) returned by any of the Claim methods.
	 * @param OutSlotTransform Transform (in world space) of the slot associated to ClaimHandle.
	 * @return Whether the transform was found and assigned to 'OutSlotTransform'
	 * @note Method will ensure on invalid FSmartObjectClaimHandle.
	 */
	UFUNCTION(BlueprintCallable, Category = "SmartObject")
	bool GetSlotTransform(const FSmartObjectClaimHandle& ClaimHandle, FTransform& OutSlotTransform) const;
	
	/**
	 * Returns the transform (in world space) of the slot associated to the given request result.
	 * @param Result A valid request result (Result.IsValid() returns true) returned by any of the Find methods.
	 * @return Transform (in world space) of the slot associated to Result.
	 * @note Method will ensure on invalid FSmartObjectRequestResult.
	 */
	TOptional<FTransform> GetSlotTransform(const FSmartObjectRequestResult& Result) const;

	/**
	 * Returns the transform (in world space) of the slot represented by the provided slot handle.
	 * @param SlotHandle Handle to a smart object slot.
	 * @return Transform (in world space) of the slot associated to SlotHandle.
	 * @note Method will ensure on invalid slot handle.
	 */
	TOptional<FTransform> GetSlotTransform(FSmartObjectSlotHandle SlotHandle) const;

	/**
	 * Returns the Activity GameplayTagContainer of the smartobject associated to the given request result.
	 * @param Result A valid request result (Result.IsValid() returns true) returned by any of the Find methods.
	 * @return FGameplayTagContainer of the slot associated to Result.
	 * @note Method will ensure on invalid FSmartObjectRequestResult.
	 */
	const FGameplayTagContainer& GetActivityTags(const FSmartObjectRequestResult& Result) const;

	/**
	 * Returns the Activity GameplayTagContainer of the smartobject represented by the provided handle.
	 * @param Handle Handle to the SmartObject.
	 * @return FGameplayTagContainer of the SmartObject associated to Handle.
	 * @note Method will ensure on invalid FSmartObjectHandle.
	 */
	const FGameplayTagContainer& GetActivityTags(const FSmartObjectHandle& Handle) const;

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

	/** Creates all runtime data using main collection */
	void InitializeRuntime();

	/** Removes all runtime data */
	void CleanupRuntime();

	/**
	 * Goes through all defined slots of smart object represented by SmartObjectRuntime
	 * and finds the first one matching the filter.
	 * @return Handle to a valid slot to use. Call IsValid on it to check if the search was successful.
	 */
	FSmartObjectSlotHandle FindSlot(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectRequestFilter& Filter) const;
	void FindSlots(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectRequestFilter& Filter, TArray<FSmartObjectSlotHandle>& OutResults, ESmartObjectSlotSearchMode SearchMode) const;

	FSmartObjectClaimHandle Claim(FSmartObjectHandle Handle, FSmartObjectSlotHandle SlotHandle);

	static const USmartObjectBehaviorDefinition* GetBehaviorDefinition(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass);

	const USmartObjectBehaviorDefinition* Use(const FSmartObjectRuntime& SmartObjectRuntime, const FSmartObjectClaimHandle& ClaimHandle, const TSubclassOf<USmartObjectBehaviorDefinition>& DefinitionClass);

	void AbortAll(FSmartObjectRuntime& SmartObjectRuntime);

	FSmartObjectSlotClaimState* GetMutableSlotState(const FSmartObjectClaimHandle& ClaimHandle);

	/** Make sure that all SmartObjectCollection actors from our associated world are registered. */
	void RegisterCollectionInstances();

	void AddToSimulation(const FSmartObjectHandle Handle, const USmartObjectDefinition& Definition, const FTransform& Transform, const FBox& Bounds);
	void AddToSimulation(const FSmartObjectCollectionEntry& Entry, const USmartObjectDefinition& Definition);
	void AddToSimulation(const USmartObjectComponent&);
	void RemoveFromSimulation(const FSmartObjectHandle Handle);
	void RemoveFromSimulation(const FSmartObjectCollectionEntry& Entry);
	void RemoveFromSimulation(const USmartObjectComponent& SmartObjectComponent);

protected:
	UPROPERTY()
	ASmartObjectCollection* MainCollection;

	UPROPERTY()
	UMassEntitySubsystem* EntitySubsystem;

	FSmartObjectOctree SmartObjectOctree;

	TMap<FSmartObjectHandle, FSmartObjectRuntime> RuntimeSmartObjects;
	TMap<FSmartObjectSlotHandle, FSmartObjectSlotClaimState> RuntimeSlotStates;

	/** Keep track of Ids associated to objects entirely created at runtime (i.e. not part of the initial collection) */
	TArray<FSmartObjectHandle> RuntimeCreatedEntries;

	uint32 NextFreeUserID = 1;

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
	const TMap<FSmartObjectHandle, FSmartObjectRuntime>& DebugGetRuntimeObjects() const { return RuntimeSmartObjects; }
	const TMap<FSmartObjectSlotHandle, FSmartObjectSlotClaimState>& DebugGetRuntimeSlots() const { return RuntimeSlotStates; }
	uint32 DebugGetNumRegisteredComponents() const { return DebugRegisteredComponents.Num(); }

	/** Debugging helper to remove all registered smart objects from the simulation */
	void DebugUnregisterAllSmartObjects();

	/** Debugging helpers to add all registered smart objects to the simulation */
	void DebugRegisterAllSmartObjects();

	/** Debugging helper to emulate the start of the simulation to create all runtime data */
	void DebugInitializeRuntime();

	/** Debugging helper to emulate the stop of the simulation to destroy all runtime data */
	void DebugCleanupRuntime();

private:
	TArray<TWeakObjectPtr<USmartObjectComponent>> DebugRegisteredComponents;
#endif // WITH_SMARTOBJECT_DEBUG
};
