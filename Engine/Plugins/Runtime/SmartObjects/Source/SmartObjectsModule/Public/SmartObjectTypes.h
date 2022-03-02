// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "Containers/UnrealString.h"
#include "EngineDefines.h"
#include "SmartObjectTypes.generated.h"

class FDebugRenderSceneProxy;

#define WITH_SMARTOBJECT_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)

SMARTOBJECTSMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogSmartObject, Warning, All);

/**
 * Indicates how Tags and TagQueries (User and Activity) from slots and parent object will be processed for find requests.
 * Tag Queries (ObjectTagFilter) from definitions tested against SmartObject instances tags are not affected.
 */
UENUM()
enum class ESmartObjectTagFilteringPolicy : uint8
{
	/**
	 * Tags and TagQueries stored in the object and slot definitions are not used by the framework to filter results.
	 * Users can access them and perform its own filtering.
	 */
	NoFilter,
	/**
	 * Activity Tags are combined (parent object and slot) and Activity TagQuery from the request will be run against the combined list.
	 * Both User TagQueries (parent object and slot) will be applied to the User Tags provided by a request.
	 */
	Combine,
	/**
	 * Activity Tags in slot (if any) will be used instead of the parent object Activity Tags when running the Activity TagQuery from a request.
	 * User TagQuery in slot (if any) will be used instead of the parent object TagQuery to run against the User Tags provided by a request.
	 * Empty Tags, or empty TagQuery on a slot indicates no override.
	 */
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
	void Reset() { *this = Invalid; }

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

	bool IsValid() const { return *this != Invalid; }
	void Reset() { *this = Invalid; }

	friend FString LexToString(const FSmartObjectHandle& Handle)
	{
		return LexToString(Handle.ID);
	}

	bool operator==(const FSmartObjectHandle& Other) const { return ID == Other.ID; }
	bool operator!=(const FSmartObjectHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FSmartObjectHandle& Handle)
	{
		return Handle.ID;
	}

private:
	/** Valid Id must be created by the collection */
 	friend class ASmartObjectCollection;

	explicit FSmartObjectHandle(const uint32 InID) : ID(InID) {}

	UPROPERTY(VisibleAnywhere, Category = SmartObject)
	uint32 ID = INDEX_NONE;

 public:
 	static const FSmartObjectHandle Invalid;
};


/**
 * Struct used to identify a runtime slot instance
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotHandle
{
	GENERATED_BODY()

public:
	FSmartObjectSlotHandle() = default;
	bool IsValid() const
	{
		return EntityHandle.IsValid();
	}

	bool operator==(const FSmartObjectSlotHandle& Other) const { return EntityHandle == Other.EntityHandle; }
	bool operator!=(const FSmartObjectSlotHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FSmartObjectSlotHandle& SlotHandle)
	{
		return GetTypeHash(SlotHandle.EntityHandle);
	}

	friend FString LexToString(const FSmartObjectSlotHandle& SlotHandle)
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
	virtual FInstancedStruct Add(const FSmartObjectHandle& Handle, const FBox& Bounds) { return FInstancedStruct(); }
	virtual void Remove(const FSmartObjectHandle& Handle, const FStructView& EntryData) {}
	virtual void Find(const FBox& QueryBox, TArray<FSmartObjectHandle>& OutResults) {}

#if UE_ENABLE_DEBUG_DRAWING
	virtual void Draw(FDebugRenderSceneProxy* DebugProxy) {}
#endif
};
