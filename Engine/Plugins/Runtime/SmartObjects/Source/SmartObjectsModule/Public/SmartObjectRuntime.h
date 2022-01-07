// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/DelegateCombinations.h"
#include "SmartObjectTypes.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectOctree.h"
#include "SmartObjectRuntime.generated.h"

/**
 * Enumeration to represent the runtime state of a slot
 */
UENUM()
enum class ESmartObjectSlotState : uint8
{
	Free,
	Claimed,
	Occupied,
	MAX
};

/**
 * Struct describing a reservation between a user and a smart object slot.
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectClaimHandle
{
	GENERATED_BODY()

	FSmartObjectClaimHandle(const FSmartObjectID InSmartObjectID, const FSmartObjectSlotIndex& InSlotIndex, const FSmartObjectUserID& InUser = {})
		: SmartObjectID(InSmartObjectID), SlotIndex(InSlotIndex), UserID(InUser)
	{}

	FSmartObjectClaimHandle()
	{}

	bool operator==(const FSmartObjectClaimHandle& Other) const
	{
		return IsValid() && Other.IsValid()
			&& SmartObjectID == Other.SmartObjectID
			&& SlotIndex == Other.SlotIndex
			&& UserID == Other.UserID;
	}

	bool operator!=(const FSmartObjectClaimHandle& Other) const
	{
		return !(*this == Other);
	}

	FString Describe() const
	{
		return FString::Printf(TEXT("Object:%s Slot:%s User:%s"), *SmartObjectID.Describe(), *SlotIndex.Describe(), *UserID.Describe());
	}

	void Invalidate() { *this = InvalidHandle; }
	bool IsValid() const
	{
		return SmartObjectID.IsValid()
			&& SlotIndex.IsValid()
			&& UserID.IsValid();
	}

	static const FSmartObjectClaimHandle InvalidHandle;

	UPROPERTY(Transient)
	FSmartObjectID SmartObjectID;

	UPROPERTY(Transient)
	FSmartObjectSlotIndex SlotIndex;

	FSmartObjectUserID UserID;
};

/** Delegate to notify when a given slot gets invalidated and the interaction must be aborted */
DECLARE_DELEGATE_TwoParams(FOnSlotInvalidated, const FSmartObjectClaimHandle&, ESmartObjectSlotState /* Current State */);

/**
 * Struct to store and manage state of a runtime instance associated to a given slot definition
 */
USTRUCT()
struct FSmartObjectSlotRuntimeData
{
	GENERATED_BODY()

public:
	/* Provide default constructor to be able to compile template instantiation 'UScriptStruct::TCppStructOps<FSmartObjectSlotRuntimeData>' */
	/* Also public to pass void 'UScriptStruct::TCppStructOps<FSmartObjectSlotRuntimeData>::ConstructForTests(void *)' */
	FSmartObjectSlotRuntimeData() {}

	ESmartObjectSlotState GetState() const { return State; }

protected:
	/** Struct could have been nested inside the subsystem but not possible with USTRUCT */
	friend class USmartObjectSubsystem;
	friend struct FSmartObjectRuntime;

	explicit FSmartObjectSlotRuntimeData(const FSmartObjectSlotIndex& InSlotIndex) : SlotIndex(InSlotIndex) {}

	bool Claim(const FSmartObjectUserID& InUser);
	
	FString Describe() const { return FString::Printf(TEXT("User:%s Slot:%s"), *User.Describe(), *SlotIndex.Describe()); }

	static const FSmartObjectSlotRuntimeData InvalidSlot;

	/** Current availability state of the slot */
	ESmartObjectSlotState State = ESmartObjectSlotState::Free;

	/** Id of the user that reserves or uses the slot */
	FSmartObjectUserID User;

	/** Index of the slot in the smart object definition to which this runtime data is associated to */
	FSmartObjectSlotIndex SlotIndex;

	/** Delegate used to notify when a slot gets invalidated. See RegisterSlotInvalidationCallback */
	FOnSlotInvalidated OnSlotInvalidatedDelegate;
};

/**
 * Struct to store and manage state of a runtime instance associated to a given smart object definition
 */
USTRUCT()
struct FSmartObjectRuntime
{
	GENERATED_BODY()

public:
	const FSmartObjectID& GetRegisteredID() const { return RegisteredID; }
	const FTransform& GetTransform() const { return Transform; }
	const USmartObjectDefinition& GetDefinition() const { checkf(Definition != nullptr, TEXT("Initialized from a valid reference from the constructor")); return *Definition; }
	 ESmartObjectSlotState GetSlotState(const uint32 SlotIndex) const;

	/* Provide default constructor to be able to compile template instantiation 'UScriptStruct::TCppStructOps<FSmartObjectRuntime>' */
	/* Also public to pass void 'UScriptStruct::TCppStructOps<FSmartObjectRuntime>::ConstructForTests(void *)' */
	FSmartObjectRuntime() : SharedOctreeID(MakeShareable(new FSmartObjectOctreeID())) {}

private:
	/** Struct could have been nested inside the subsystem but not possible with USTRUCT */
	friend class USmartObjectSubsystem;

	explicit FSmartObjectRuntime(const USmartObjectDefinition& Definition);

	const FGameplayTagContainer& GetTags() const { return Tags; }

	void SetTransform(const FTransform& Value) { Transform = Value; }

	void SetRegisteredID(const FSmartObjectID Value) { RegisteredID = Value; }

	const FSmartObjectOctreeIDSharedRef& GetSharedOctreeID() const { return SharedOctreeID; }
	void SetOctreeID(const FSmartObjectOctreeIDSharedRef Value) { SharedOctreeID = Value; }

	FString Describe() const { return FString::Printf(TEXT("Instance using defintion \'%s\' Reg: %s"), *GetDefinition().Describe(), *LexToString(SharedOctreeID->ID.IsValidId())); }

	/**
	 * @param OutFreeSlots function will set 'false' at taken slots' indices
	 * @return the number of free slots found
	 */
	uint32 FindFreeSlots(TBitArray<>& OutFreeSlots) const;

	/**
	 * Reserves the slot associated to the provided handle by marking it as 'claimed'.
	 * @param ClaimHandle Handle identifying a user/slot pair
	 * @return whether or not the slot was successfully claimed
	 */
	bool ClaimSlot(const FSmartObjectClaimHandle& ClaimHandle);

	/**
	 * Mark an available slot as 'occupied'.
	 * @param ClaimHandle Handle identifying a user/slot pair
	 * @return whether or not the slot was successfully marked as 'occupied'
	 */
	 bool UseSlot(const FSmartObjectClaimHandle& ClaimHandle);

	/**
	 * Releases the slot associated to the provided handle by marking it as 'free'.
	 * The operation will ensure if the slot is not currently claimed or in use by the user associated to the handle.
	 * @param ClaimHandle A valid handle identifying a user/slot pair
	 * @param bAborted Indicates if the slot must be released because the slot has been invalidated by the system (e.g. unregistered object)
	 * @return whether or not the slot was successfully freed
	 */
	bool ReleaseSlot(const FSmartObjectClaimHandle& ClaimHandle, const bool bAborted);

	/** Runtime data associated to each defined slot */
	UPROPERTY()
	TArray<FSmartObjectSlotRuntimeData> SlotsRuntimeData;

	/** Associated smart object definition */
	UPROPERTY()
	const USmartObjectDefinition* Definition = nullptr;

	/** Instance specific transform */
	FTransform Transform;

	/** Tags applied to the current instance */
	FGameplayTagContainer Tags;

	/** RegisteredID != FSmartObject::InvalidID when registered with SmartObjectSubsystem */
	FSmartObjectID RegisteredID;

	/** Reference to the associated octree node ID */
	FSmartObjectOctreeIDSharedRef SharedOctreeID;
};
