// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstancedStructArray.h"
#include "StructView.h"
#include "StateTreeEvents.h"
#include "StateTreeInstanceData.generated.h"

/**
 * StateTree instance data is used to store the runtime state of a StateTree.
 * The layout of the data is described in a FStateTreeInstanceDataLayout.
 *
 * Note: Serialization is supported only for FArchive::IsModifyingWeakAndStrongReferences(), that is replacing object references.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeInstanceData
{
	GENERATED_BODY()

	FStateTreeInstanceData() = default;
	~FStateTreeInstanceData() { Reset(); }

	/** Initializes the array with specified items. */
	void Init(UObject& InOwner, TConstArrayView<FInstancedStruct> InStructs, TConstArrayView<UObject*> InObjects);
	void Init(UObject& InOwner, TConstArrayView<FConstStructView> InStructs, TConstArrayView<UObject*> InObjects);

	/** Appends new items to the instance. */
	void Append(UObject& InOwner, TConstArrayView<FInstancedStruct> InStructs, TConstArrayView<UObject*> InObjects);
	void Append(UObject& InOwner, TConstArrayView<FConstStructView> InStructs, TConstArrayView<UObject*> InObjects);

	/** Prunes the array sizes to specified lengths. */
	void Prune(const int32 NumStructs, const int32 NumObjects);

	/** Shares the layout from another instance data, and copies the data over. */
	void CopyFrom(UObject& InOwner, const FStateTreeInstanceData& InOther);

	/** Resets the data to empty. */
	void Reset();

	/** @return true if the instance is correctly initialized. */
	bool IsValid() const { return InstanceStructs.Num() > 0 || InstanceObjects.Num() > 0; }

	/** @return Number of items in the instance data. */
	int32 NumStructs() const { return InstanceStructs.Num(); }

	/** @return true if the specified index is valid index into the struct data container. */
	bool IsValidStructIndex(const int32 Index) const { return InstanceStructs.IsValidIndex(Index); }
	
	/** @return mutable view to the struct at specified index. */
	FStructView GetMutableStruct(const int32 Index) const { return InstanceStructs[Index]; }

	/** @return const view to the struct at specified index. */
	FConstStructView GetStruct(const int32 Index) const { return InstanceStructs[Index]; }

	/** @return number of instance objects */
	int32 NumObjects() const { return InstanceObjects.Num(); }

	/** @return pointer to an instance object   */
	UObject* GetMutableObject(const int32 Index) const { return InstanceObjects[Index]; }

	/** @return const pointer to an instance object   */
	const UObject* GetObject(const int32 Index) const { return InstanceObjects[Index]; }

	/** @return array to store unprocessed events. */
	UE_DEPRECATED(5.2, "Use GetEventQueue() instead.")
	TArray<FStateTreeEvent>& GetEvents();

	/** @return reference to the event queue. */
	FStateTreeEventQueue& GetEventQueue() { return EventQueue; }
	
	int32 GetEstimatedMemoryUsage() const;
	int32 GetNumItems() const;
	
	/** Type traits */
	bool Identical(const FStateTreeInstanceData* Other, uint32 PortFlags) const;
	
private:
	
	/** Struct instances */
	UPROPERTY()
	FInstancedStructArray InstanceStructs;

	/** Object instances. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> InstanceObjects;

	/** Events */
	UPROPERTY()
	FStateTreeEventQueue EventQueue;
};

template<>
struct TStructOpsTypeTraits<FStateTreeInstanceData> : public TStructOpsTypeTraitsBase2<FStateTreeInstanceData>
{
	enum
	{
		WithIdentical = true,
	};
};

/**
 * Stores indexed reference to a instance data struct.
 * The instance data structs may be relocated when the instance data composition changed. For that reason you cannot store pointers to the instance data.
 * This is often needed for example when dealing with delegate lambda's. This helper struct stores the instance data as index to the instance data array.
 * That way we can access the instance data even of the array changes.
 *
 * You generally do not use this directly, but via FStateTreeExecutionContext.
 *
 *	EStateTreeRunStatus FTestTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
 *	{
 *		FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
 *
 *		Context.GetWorld()->GetTimerManager().SetTimer(
 *	        InstanceData.TimerHandle,
 *	        [InstanceDataRef = Context.GetInstanceDataStructRef()]()
 *	        {
 *	            FInstanceDataType& InstanceData = *InstanceDataRef;
 *	            ...
 *	        },
 *	        Delay, true);
 *
 *	    return EStateTreeRunStatus::Running;
 *	}
 */
template <typename T>
struct TStateTreeInstanceDataStructRef
{
	TStateTreeInstanceDataStructRef(FStateTreeInstanceData& InInstanceData, const T& InstanceDataStruct)
		: InstanceData(InInstanceData)
	{
		const FConstStructView InstanceDataStructView = FConstStructView::template Make(InstanceDataStruct);
		// Find struct in the instance data.
		for (int32 Index = 0; Index < InstanceData.NumStructs(); Index++)
		{
			if (InstanceData.GetStruct(Index) == InstanceDataStructView)
			{
				StructIndex = Index;
				break;
			}
		}
		check(StructIndex != INDEX_NONE);
	}

	bool IsValid() const { return InstanceData.IsValidStructIndex(StructIndex); }

	T& operator*() const
	{
		check(IsValid());
		const FStructView Struct = InstanceData.GetMutableStruct(StructIndex);
		check(Struct.GetScriptStruct() == TBaseStructure<T>::Get());
		return *reinterpret_cast<T*>(Struct.GetMutableMemory());
	}

protected:
	FStateTreeInstanceData& InstanceData;
	int32 StructIndex = INDEX_NONE;
};