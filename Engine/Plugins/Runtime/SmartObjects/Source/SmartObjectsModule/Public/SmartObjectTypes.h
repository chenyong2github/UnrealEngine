// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "EngineDefines.h"
#include "GameplayTagContainer.h"
#include "Math/Box.h"
#include "SmartObjectTypes.generated.h"

class FDebugRenderSceneProxy;
class UNavigationQueryFilter;

#define WITH_SMARTOBJECT_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)

SMARTOBJECTSMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogSmartObject, Warning, All);

namespace UE::SmartObjects
{
#if WITH_EDITORONLY_DATA
	inline const FName WithSmartObjectTag = FName("WithSmartObject");
#endif // WITH_EDITORONLY_DATA
}

/** Indicates how Tags from slots and parent object are combined to be evaluated by a TagQuery from a find request. */
UENUM()
enum class ESmartObjectTagMergingPolicy : uint8
{
	/** Tags are combined (parent object and slot) and TagQuery from the request will be run against the combined list. */
	Combine,
	/** Tags in slot (if any) will be used instead of the parent object Tags when running the TagQuery from a request. Empty Tags on a slot indicates no override. */
	Override
};


/** Indicates how TagQueries from slots and parent object will be processed against Tags from a find request. */
UENUM()
enum class ESmartObjectTagFilteringPolicy : uint8
{
	/** TagQueries in the object and slot definitions are not used by the framework to filter results. Users can access them and perform its own filtering. */
	NoFilter,
	/** Both TagQueries (parent object and slot) will be applied to the Tags provided by a request. */
	Combine,
	/** TagQuery in slot (if any) will be used instead of the parent object TagQuery to run against the Tags provided by a request. EmptyTagQuery on a slot indicates no override. */
	Override
};


/**
 * Handle to a smartobject user.
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectUserHandle
{
	GENERATED_BODY()

public:
	FSmartObjectUserHandle() = default;

	bool IsValid() const { return *this != Invalid; }
	void Invalidate() { *this = Invalid; }

	bool operator==(const FSmartObjectUserHandle& Other) const { return ID == Other.ID; }
	bool operator!=(const FSmartObjectUserHandle& Other) const { return !(*this == Other); }

	friend FString LexToString(const FSmartObjectUserHandle& UserHandle)
	{
		return LexToString(UserHandle.ID);
	}

private:
	/** Valid Id must be created by the subsystem */
	friend class USmartObjectSubsystem;

	explicit FSmartObjectUserHandle(const uint32 InID) : ID(InID) {}

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	uint32 ID = INDEX_NONE;

public:
	static const FSmartObjectUserHandle Invalid;
};


/**
 * Handle to a registered smartobject.
 * Internal IDs are assigned in editor by the collection and then serialized for runtime.
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectHandle
{
	GENERATED_BODY()

public:
	FSmartObjectHandle() {}

	/**
	 * Indicates that the handle was properly assigned but doesn't guarantee that the associated object is still accessible.
	 * This information requires a call to `USmartObjectSubsystem::IsObjectValid` using the handle.
	 */
	bool IsValid() const { return *this != Invalid; }
	void Invalidate() { *this = Invalid; }

	friend FString LexToString(const FSmartObjectHandle Handle)
	{
		return LexToString(Handle.ID);
	}

	bool operator==(const FSmartObjectHandle Other) const { return ID == Other.ID; }
	bool operator!=(const FSmartObjectHandle Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FSmartObjectHandle Handle)
	{
		return Handle.ID;
	}

private:
	/** Valid Id must be created by the collection */
	friend struct FSmartObjectHandleFactory;

	explicit FSmartObjectHandle(const uint32 InID) : ID(InID) {}

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	uint32 ID = INDEX_NONE;

 public:
 	static const FSmartObjectHandle Invalid;
};


/**
 * Struct used to identify a runtime slot instance
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectSlotHandle
{
	GENERATED_BODY()

public:
	FSmartObjectSlotHandle() = default;

	/**
	 * Indicates that the handle was properly assigned but doesn't guarantee that the associated slot is still accessible.
	 * This information requires a call to `USmartObjectSubsystem::IsSlotValid` using the handle.
	 */
	bool IsValid() const { return EntityHandle.IsValid(); }
	void Invalidate() { EntityHandle.Reset(); }

	bool operator==(const FSmartObjectSlotHandle Other) const { return EntityHandle == Other.EntityHandle; }
	bool operator!=(const FSmartObjectSlotHandle Other) const { return !(*this == Other); }
	/** Has meaning only for sorting purposes */
	bool operator<(const FSmartObjectSlotHandle Other) const { return EntityHandle < Other.EntityHandle; }

	friend uint32 GetTypeHash(const FSmartObjectSlotHandle SlotHandle)
	{
		return GetTypeHash(SlotHandle.EntityHandle);
	}

	friend FString LexToString(const FSmartObjectSlotHandle SlotHandle)
	{
		return LexToString(SlotHandle.EntityHandle.Index);
	}

protected:
	/** Do not expose the EntityHandle anywhere else than SlotView or the Subsystem. */
	friend class USmartObjectSubsystem;
	friend struct FSmartObjectSlotView;

	FSmartObjectSlotHandle(const FMassEntityHandle InEntityHandle) : EntityHandle(InEntityHandle)
	{
	}

	operator FMassEntityHandle() const
	{
		return EntityHandle;
	}

	/** The MassEntity associated to the slot */
	FMassEntityHandle EntityHandle;
};


/**
 * This is the base struct to inherit from to store custom definition data within the main slot definition
 */
USTRUCT(meta=(Hidden))
struct SMARTOBJECTSMODULE_API FSmartObjectSlotDefinitionData
{
	GENERATED_BODY()
	virtual ~FSmartObjectSlotDefinitionData() {}
};

/**
 * This is the base struct to inherit from to store custom state data associated to a slot
 */
USTRUCT(meta=(Hidden))
struct SMARTOBJECTSMODULE_API FSmartObjectSlotStateData : public FMassFragment
{
	GENERATED_BODY()
};

/**
 * This is the base struct to inherit from to store some data associated to a specific entry in the spatial representation structure
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSpatialEntryData
{
	GENERATED_BODY()
};

/**
 * Base class for space partitioning structure that can be used to store smart object locations
 */
UCLASS(Abstract)
class SMARTOBJECTSMODULE_API USmartObjectSpacePartition : public UObject
{
	GENERATED_BODY()

public:
	virtual void SetBounds(const FBox& Bounds) {}
	virtual FInstancedStruct Add(const FSmartObjectHandle Handle, const FBox& Bounds) { return FInstancedStruct(); }
	virtual void Remove(const FSmartObjectHandle Handle, FStructView EntryData) {}
	virtual void Find(const FBox& QueryBox, TArray<FSmartObjectHandle>& OutResults) {}

#if UE_ENABLE_DEBUG_DRAWING
	virtual void Draw(FDebugRenderSceneProxy* DebugProxy) {}
#endif
};


/**
 * Helper struct to wrap basic functionalities to store the index of a slot in a SmartObject definition
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectSlotIndex
{
	GENERATED_BODY()

	explicit FSmartObjectSlotIndex(const int32 InSlotIndex = INDEX_NONE) : Index(InSlotIndex) {}

	bool IsValid() const { return Index != INDEX_NONE; }
	void Invalidate() { Index = INDEX_NONE; }

	operator int32() const { return Index; }

	bool operator==(const FSmartObjectSlotIndex& Other) const { return Index == Other.Index; }
	friend FString LexToString(const FSmartObjectSlotIndex& SlotIndex) { return FString::Printf(TEXT("[Slot:%d]"), SlotIndex.Index); }

private:
	UPROPERTY(Transient)
	int32 Index = INDEX_NONE;
};

/**
 * Reference to a specific Smart Object slot in a Smart Object Definition.
 * When placed on a slot definition data, the Index is resolved automatically when changed, on load and save. 
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotReference
{
	GENERATED_BODY()

	static constexpr uint8 InvalidValue = 0xff;

	bool IsValid() const { return Index != InvalidValue; }

	int32 GetIndex() const { return Index == InvalidValue ? INDEX_NONE : Index; }
	
	void SetIndex(const int32 InIndex)
	{
		if (InIndex >= 0 && InIndex < InvalidValue)
		{
			Index = (uint8)InIndex;
		}
		else
		{
			Index = InvalidValue; 
		}
	}

#if WITH_EDITORONLY_DATA
	const FGuid& GetSlotID() const { return SlotID; }
#endif
	
private:
	UPROPERTY()
	uint8 Index = InvalidValue;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FGuid SlotID;
#endif // WITH_EDITORONLY_DATA

	friend class FSmartObjectSlotReferenceDetails;
};



/** Indicates how TagQueries from slots and parent object will be processed against Tags from a find request. */
UENUM()
enum class ESmartObjectTraceType : uint8
{
	ByChannel,
	ByProfile,
	ByObjectTypes,
};

/** Struct used to define how traces should be handled. */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectTraceParams
{
	GENERATED_BODY()

	FSmartObjectTraceParams() = default;
	
	explicit FSmartObjectTraceParams(const ETraceTypeQuery InTraceChanel)
		: Type(ESmartObjectTraceType::ByChannel)
		, TraceChannel(InTraceChanel)
	{
	}

	explicit FSmartObjectTraceParams(TConstArrayView<EObjectTypeQuery> InObjectTypes)
		: Type(ESmartObjectTraceType::ByObjectTypes)
	{
		for (const EObjectTypeQuery ObjectType : InObjectTypes)
		{
			ObjectTypes.Add(ObjectType);
		}
	}

	explicit FSmartObjectTraceParams(const FCollisionProfileName InCollisionProfileName)
		: Type(ESmartObjectTraceType::ByProfile)
		, CollisionProfile(InCollisionProfileName)
	{
	}

	/** Type of trace to use. */
	UPROPERTY(EditAnywhere, Category = "Default")
	ESmartObjectTraceType Type = ESmartObjectTraceType::ByChannel;

	/** Trace channel to use to determine collisions. */
	UPROPERTY(EditAnywhere, Category = "Default", meta = (EditCondition = "Type == ESmartObjectTraceType::ByChannel", EditConditionHides))
	TEnumAsByte<ETraceTypeQuery> TraceChannel = ETraceTypeQuery::TraceTypeQuery1;

	/** Object types to use to determine collisions. */
	UPROPERTY(EditAnywhere, Category = "Default", meta = (EditCondition = "Type == ESmartObjectTraceType::ByObjectTypes", EditConditionHides))
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;

	/** Collision profile to use to determine collisions. */
	UPROPERTY(EditAnywhere, Category = "Default", meta = (EditCondition = "Type == ESmartObjectTraceType::ByProfile", EditConditionHides))
	FCollisionProfileName CollisionProfile;

	/** Whether we should trace against complex collision */
	UPROPERTY(EditAnywhere, Category = "Default")
	bool bTraceComplex = false;
};

/**
 * Class used to define settings for Smart Object navigation and collision validation. 
 * The values of the CDO are used, the users are expected to derive from this class to create custom settings. 
 */
UCLASS(Blueprintable, Abstract)
class SMARTOBJECTSMODULE_API USmartObjectSlotValidationFilter : public UObject
{
	GENERATED_BODY()

public:
	/** @return navigation filter class to be used for navigation checks. */
	TSubclassOf<UNavigationQueryFilter> GetNavigationFilter() const { return NavigationFilter; }

	/** @return search extents used to define how far the validation can move the points. */
	FVector GetSearchExtents() const { return SearchExtents; }

	/** @return trace parameters for finding ground location. */
	const FSmartObjectTraceParams& GetGroundTraceParameters() const { return GroundTraceParameters; }

	/** @return trace parameters for testing if there are collision transitioning from navigation location to slot location. */
	const FSmartObjectTraceParams& GetTransitionTraceParameters() const { return TransitionTraceParameters; }
	
protected:
	/** Navigation filter used to  */
	UPROPERTY(EditAnywhere, Category = "Default")
	const TSubclassOf<UNavigationQueryFilter> NavigationFilter;

	/** How far we allow the validated location to be from the specified navigation location. */
	UPROPERTY(EditAnywhere, Category = "Default")
	FVector SearchExtents = FVector(5.0f, 5.0f, 40.0f);

	/** Trace parameters used for finding navigation location on ground. */
	UPROPERTY(EditAnywhere, Category = "Default")
	FSmartObjectTraceParams GroundTraceParameters;

	/** Trace parameters user for checking if the transition between navigation location and slot is unblocked. */
	UPROPERTY(EditAnywhere, Category = "Default")
	FSmartObjectTraceParams TransitionTraceParameters;
};

/**
 * Describes how Smart Object or slot was changed.
 */
UENUM(BlueprintType)
enum class ESmartObjectChangeReason : uint8
{
	/** No Change. */
	None,
	/** External event sent. */
	OnEvent,
	/** A tag was added. */
	OnTagAdded,
	/** A tag was removed. */
	OnTagRemoved,
	/** Slot was claimed. */
	OnClaimed,
	/** Slot is now occupied*/
	OnOccupied,
	/** Slot claim was released. */
	OnReleased,
	/** Slot was enabled. */
	OnSlotEnabled,
	/** Slot was disabled. */
	OnSlotDisabled,
	/** Object was enabled. */
	OnObjectEnabled,
	/** Object was disabled. */
	OnObjectDisabled
};

/**
 * Strict describing a change in Smart Object or Slot. 
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectEventData
{
	GENERATED_BODY()

	/** Handle to the changed Smart Object. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "SmartObject")
	FSmartObjectHandle SmartObjectHandle;

	/** Handle to the changed slot, if invalid, the event is for the object. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "SmartObject")
	FSmartObjectSlotHandle SlotHandle;

	/** Change reason. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "SmartObject")
	ESmartObjectChangeReason Reason = ESmartObjectChangeReason::None;

	/** Added/Removed tag, or event tag, depending on Reason. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "SmartObject")
	FGameplayTag Tag;

	/**
	 * Event payload.
	 * For external event (i.e. SendSlotEvent) payload is provided by the caller.
	 * For internal event types (e.g. OnClaimed, OnReleased, etc.)
	 * payload is the user data struct provided on claim.
	 **/
	FConstStructView EventPayload;
};

/**
 * Struct that can be used to pass data to the find or filtering methods.
 * Properties will be used as user data to fill values expected by the world condition schema
 * specified by the smart object definition.
 *		e.g. FilterSlotsBySelectionConditions(SlotHandles, FConstStructView::Make(FSmartObjectActorUserData(Pawn)));
 *
 * It can be inherited from to provide additional data to another world condition schema inheriting
 * from USmartObjectWorldConditionSchema.
 *	e.g.
 *		UCLASS()
 *		class USmartObjectWorldConditionExtendedSchema : public USmartObjectWorldConditionSchema
 *		{
 *			...
 *			USmartObjectWorldConditionExtendedSchema(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
 *			{
 *				OtherActorRef = AddContextDataDesc(TEXT("OtherActor"), AActor::StaticClass(), EWorldConditionContextDataType::Dynamic);
 *			}
 *			
 *			FWorldConditionContextDataRef OtherActorRef;
 *		};
 *
 *		USTRUCT()
 *		struct FSmartObjectActorExtendedUserData : public FSmartObjectActorUserData
 *		{
 *			UPROPERTY()
 *			TWeakObjectPtr<const AActor> OtherActor = nullptr;
 *		}
 *
 * The struct can also be used to be added to a Smart Object slot when it gets claimed.
 *		e.g. Claim(SlotHandle, FConstStructView::Make(FSmartObjectActorUserData(Pawn)));
 */
USTRUCT()
struct FSmartObjectActorUserData
{
	GENERATED_BODY()

	FSmartObjectActorUserData() = default;
	explicit FSmartObjectActorUserData(const AActor* InUserActor) : UserActor(InUserActor) {}

	UPROPERTY()
	TWeakObjectPtr<const AActor> UserActor = nullptr;
};

/** Delegate called when Smart Object or Slot is changed. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSmartObjectEvent, const FSmartObjectEventData& /*Event*/);
