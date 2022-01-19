// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "MassEntityView.h"
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
	Invalid,
	Free,
	Claimed,
	Occupied
};

/**
 * Struct describing a reservation between a user and a smart object slot.
 */
USTRUCT(BlueprintType)
struct SMARTOBJECTSMODULE_API FSmartObjectClaimHandle
{
	GENERATED_BODY()

	FSmartObjectClaimHandle(const FSmartObjectHandle InSmartObjectHandle, const FSmartObjectSlotHandle InSlotHandle, const FSmartObjectUserHandle& InUser)
		: SmartObjectHandle(InSmartObjectHandle), SlotHandle(InSlotHandle), UserHandle(InUser)
	{}

	FSmartObjectClaimHandle()
	{}

	bool operator==(const FSmartObjectClaimHandle& Other) const
	{
		return IsValid() && Other.IsValid()
			&& SmartObjectHandle == Other.SmartObjectHandle
			&& SlotHandle == Other.SlotHandle
			&& UserHandle == Other.UserHandle;
	}

	bool operator!=(const FSmartObjectClaimHandle& Other) const
	{
		return !(*this == Other);
	}

	friend FString LexToString(const FSmartObjectClaimHandle& Handle)
	{
		return FString::Printf(TEXT("Object:%s Slot:%s User:%s"), *LexToString(Handle.SmartObjectHandle), *LexToString(Handle.SlotHandle), *LexToString(Handle.UserHandle));
	}

	void Invalidate() { *this = InvalidHandle; }
	bool IsValid() const
	{
		return SmartObjectHandle.IsValid()
			&& SlotHandle.IsValid()
			&& UserHandle.IsValid();
	}

	static const FSmartObjectClaimHandle InvalidHandle;

	UPROPERTY(Transient)
	FSmartObjectHandle SmartObjectHandle;

	UPROPERTY(Transient)
	FSmartObjectSlotHandle SlotHandle;

	UPROPERTY(Transient)
	FSmartObjectUserHandle UserHandle;
};

/**
 * Runtime data holding the final slot transform (i.e. parent transform applied on slot local offset and rotation)
 */
USTRUCT()
struct SMARTOBJECTSMODULE_API  FSmartObjectSlotTransform : public FSmartObjectSlotStateData
{
	GENERATED_BODY()

	const FTransform& GetTransform() const { return Transform; }
	FTransform& GetMutableTransform() { return Transform; }

	void SetTransform(const FTransform& InTransform) { Transform = InTransform; }

protected:

	UPROPERTY(Transient)
	FTransform Transform;
};

/** Delegate to notify when a given slot gets invalidated and the interaction must be aborted */
DECLARE_DELEGATE_TwoParams(FOnSlotInvalidated, const FSmartObjectClaimHandle&, ESmartObjectSlotState /* Current State */);

/**
 * Struct to store and manage state of a runtime instance associated to a given slot definition
 */
USTRUCT()
struct FSmartObjectSlotClaimState
{
	GENERATED_BODY()

public:
	/* Provide default constructor to be able to compile template instantiation 'UScriptStruct::TCppStructOps<FSmartObjectSlotState>' */
	/* Also public to pass void 'UScriptStruct::TCppStructOps<FSmartObjectSlotState>::ConstructForTests(void *)' */
	FSmartObjectSlotClaimState() {}

	ESmartObjectSlotState GetState() const { return State; }

protected:
	/** Struct could have been nested inside the subsystem but not possible with USTRUCT */
	friend class USmartObjectSubsystem;
	friend struct FSmartObjectRuntime;

	bool Claim(const FSmartObjectUserHandle& InUser);
	bool Release(const FSmartObjectClaimHandle& ClaimHandle, const bool bAborted);
	
	friend FString LexToString(const FSmartObjectSlotClaimState& ClaimState)
	{
		return FString::Printf(TEXT("User:%s State:%s"), *LexToString(ClaimState.User), *UEnum::GetValueAsString(ClaimState.State));
	}

	/** Handle to the user that reserves or uses the slot */
	FSmartObjectUserHandle User;

	/** Delegate used to notify when a slot gets invalidated. See RegisterSlotInvalidationCallback */
	FOnSlotInvalidated OnSlotInvalidatedDelegate;

	/** Current availability state of the slot */
	ESmartObjectSlotState State = ESmartObjectSlotState::Free;
};

/**
 * Struct to store and manage state of a runtime instance associated to a given smart object definition
 */
USTRUCT()
struct FSmartObjectRuntime
{
	GENERATED_BODY()

public:
	const FSmartObjectHandle& GetRegisteredID() const { return RegisteredHandle; }
	const FTransform& GetTransform() const { return Transform; }
	const USmartObjectDefinition& GetDefinition() const { checkf(Definition != nullptr, TEXT("Initialized from a valid reference from the constructor")); return *Definition; }

	/* Provide default constructor to be able to compile template instantiation 'UScriptStruct::TCppStructOps<FSmartObjectRuntime>' */
	/* Also public to pass void 'UScriptStruct::TCppStructOps<FSmartObjectRuntime>::ConstructForTests(void *)' */
	FSmartObjectRuntime() : SharedOctreeID(MakeShareable(new FSmartObjectOctreeID())) {}

private:
	/** Struct could have been nested inside the subsystem but not possible with USTRUCT */
	friend class USmartObjectSubsystem;

	explicit FSmartObjectRuntime(const USmartObjectDefinition& Definition);

	const FGameplayTagContainer& GetTags() const { return Tags; }

	void SetTransform(const FTransform& Value) { Transform = Value; }

	void SetRegisteredID(const FSmartObjectHandle Value) { RegisteredHandle = Value; }

	const FSmartObjectOctreeIDSharedRef& GetSharedOctreeID() const { return SharedOctreeID; }
	void SetOctreeID(const FSmartObjectOctreeIDSharedRef Value) { SharedOctreeID = Value; }

	friend FString LexToString(const FSmartObjectRuntime& Runtime)
	{
		return FString::Printf(TEXT("Instance using defintion \'%s\' Reg: %s"),
			*LexToString(Runtime.GetDefinition()),
			*LexToString(Runtime.SharedOctreeID->ID.IsValidId()));
	}

	/** Runtime SlotHandles associated to each defined slot */
	TArray<FSmartObjectSlotHandle> SlotHandles;

	/** Associated smart object definition */
	UPROPERTY()
	const USmartObjectDefinition* Definition = nullptr;

	/** Instance specific transform */
	FTransform Transform;

	/** Tags applied to the current instance */
	FGameplayTagContainer Tags;

	/** RegisteredHandle != FSmartObjectHandle::Invalid when registered with SmartObjectSubsystem */
	FSmartObjectHandle RegisteredHandle;

	/** Reference to the associated octree node ID */
	FSmartObjectOctreeIDSharedRef SharedOctreeID;
};

USTRUCT()
struct SMARTOBJECTSMODULE_API FSmartObjectSlotView
{
	GENERATED_BODY()

public:
	FSmartObjectSlotView() = default;

	FSmartObjectSlotHandle GetSlotHandle() const { return EntityView.GetEntity(); }

	/**
	 * Returns a reference to the slot state data of the specified type.
	 * Method will fail a check if the slot doesn't have the given type.
	 */
	template<typename T>
	T& GetStateData() const
	{
		static_assert(TIsDerivedFrom<T, FSmartObjectSlotStateData>::IsDerived,
			"Given struct doesn't represent a valid runtime data type. Make sure to inherit from FSmartObjectSlotStateData or one of its child-types.");

		return EntityView.GetFragmentData<T>();
	}

	/**
	 * Returns a pointer to the slot state data of the specified type.
	 * Method will return null if the slot doesn't have the given type.
	 */
	template<typename T>
	T* GetStateDataPtr() const
	{
		static_assert(TIsDerivedFrom<T, FSmartObjectSlotStateData>::IsDerived,
					"Given struct doesn't represent a valid runtime data type. Make sure to inherit from FSmartObjectSlotStateData or one of its child-types.");

		return EntityView.GetFragmentDataPtr<T>();
	}

	/**
	 * Returns a reference to the main definition of the slot.
	 * Method will fail a check if called on an invalid SlotView.
	 */
	const FSmartObjectSlotDefinition& GetDefinition() const
	{
		checkf(EntityView.IsSet(), TEXT("Definition can only be accessed through a valid SlotView"));
		return *(EntityView.GetConstSharedFragmentData<FSmartObjectSlotDefinitionFragment>().SlotDefinition);
	}

	/**
	 * Returns a reference to the definition data of the specified type from the main slot definition.
	 * Method will fail a check if the slot definition doesn't contain the given type.
	 */
	template<typename T>
	const T& GetDefinitionData() const
	{
		static_assert(TIsDerivedFrom<T, FSmartObjectSlotDefinitionData>::IsDerived,
					"Given struct doesn't represent a valid definition data type. Make sure to inherit from FSmartObjectSlotDefinitionData or one of its child-types.");

		const FSmartObjectSlotDefinition& SlotDefinition = GetDefinition();
		for (const FInstancedStruct& Data : SlotDefinition.Data)
		{
			if (Data.GetScriptStruct()->IsChildOf(T::StaticStruct()))
			{
				return Data.Get<T>();
			}
		}

		return nullptr;
	}

	/**
	 * Returns a pointer to the definition data of the specified type from the main slot definition.
	 * Method will return null if the slot doesn't contain the given type.
	 */
	template<typename T>
	const T* GetDefinitionDataPtr() const
	{
		static_assert(TIsDerivedFrom<T, FSmartObjectSlotDefinitionData>::IsDerived,
					"Given struct doesn't represent a valid definition data type. Make sure to inherit from FSmartObjectSlotDefinitionData or one of its child-types.");

		const FSmartObjectSlotDefinition& SlotDefinition = GetDefinition();
		for (const FInstancedStruct& Data : SlotDefinition.Data)
		{
			if (Data.GetScriptStruct()->IsChildOf(T::StaticStruct()))
			{
				return Data.GetPtr<T>();
			}
		}

		return nullptr;
	}

private:
	friend class USmartObjectSubsystem;

	FSmartObjectSlotView(const UMassEntitySubsystem& EntitySubsystem, const FSmartObjectSlotHandle SlotHandle) : EntityView(EntitySubsystem, SlotHandle.EntityHandle)
	{
	}

	FMassEntityView EntityView;
};
