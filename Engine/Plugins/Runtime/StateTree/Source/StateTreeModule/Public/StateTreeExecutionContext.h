// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTree.h"
#include "InstancedStruct.h"
#include "Containers/StaticArray.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeExecutionContext.generated.h"

struct FStateTreeEvaluatorBase;
struct FStateTreeTaskBase;
struct FStateTreeConditionBase;

USTRUCT()
struct STATETREEMODULE_API FStateTreeExecutionState
{
	GENERATED_BODY()

	/** Currently active state */
	FStateTreeHandle CurrentState = FStateTreeHandle::Invalid;

	/** The index of the task that failed during enter state. Exit state uses it to call ExitState() symmetrically. */
	uint16 EnterStateFailedTaskIndex = INDEX_NONE;

	/** Result of last tick */
	EStateTreeRunStatus LastTickStatus = EStateTreeRunStatus::Failed;

	/** Running status of the instance */
	EStateTreeRunStatus TreeRunStatus = EStateTreeRunStatus::Unset;

	/** Delayed transition handle, if exists */
	int16 GatedTransitionIndex = INDEX_NONE;

	/** Running time of the delayed transition */
	float GatedTransitionTime = 0.0f;

	/** Object instances, ref counting handled manually. */
	TArray<UObject*> InstanceObjects;
};

UENUM()
enum class EStateTreeStorage : uint8
{
	/** Execution context has internal storage */ 
	Internal,
	/** Execution context assumes external storage */
	External,
};

/**
 * Runs StateTrees defined in UStateTree asset.
 * Uses constant data from StateTree, keeps local storage of variables, and creates instanced Evaluators and Tasks.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeExecutionContext
{
	GENERATED_BODY()

public:
	FStateTreeExecutionContext();
	virtual ~FStateTreeExecutionContext();

	/** Initializes the StateTree instance to be used with specific owner and StateTree asset. */
	bool Init(UObject& InOwner, const UStateTree& InStateTree, const EStateTreeStorage InStorageType);

	/**  Initializes external storage (no need to call, if using internal storage). */
	bool InitInstanceData(UObject& InOwner, FStateTreeDataView ExternalStorage) const;

	/** Returns the StateTree asset in use. */
	const UStateTree* GetStateTree() const { return StateTree; }

	/** @return The owner of the context */
	UObject* GetOwner() const { return Owner; }
	/** @return The world of the owner or nullptr if the owner is not set. */ 
	UWorld* GetWorld() const { return Owner ? Owner->GetWorld() : nullptr; };

	/** @return True of the the execution context is valid and initialized. */ 
	bool IsValid() const { return Owner != nullptr && StateTree != nullptr; }
	
	/** Start executing. */
	EStateTreeRunStatus Start(FStateTreeDataView ExternalStorage = FStateTreeDataView());
	/** Stop executing. */
	EStateTreeRunStatus Stop(FStateTreeDataView ExternalStorage = FStateTreeDataView());

	/** Tick the state tree logic. */
	EStateTreeRunStatus Tick(const float DeltaTime, FStateTreeDataView ExternalStorage = FStateTreeDataView());

	/** @return Pointer to a State or null if state not found */ 
	const FBakedStateTreeState* GetStateFromHandle(const FStateTreeHandle StateHandle) const
	{
		return (StateTree && StateTree->States.IsValidIndex(StateHandle.Index)) ? &StateTree->States[StateHandle.Index] : nullptr;
	}

	/** @return Array view to external data descriptors associated with this context. Note: Init() must be called before calling this method. */
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const
	{
		check(StateTree);
		return StateTree->ExternalDataDescs;
	}

	/** @return True if all required external data pointers are set. */ 
	bool AreExternalDataViewsValid() const
	{
		check(StateTree);
		bool bResult = true;
		for (const FStateTreeExternalDataDesc& DataDesc : StateTree->ExternalDataDescs)
		{
			const FStateTreeDataView& DataView = DataViews[DataDesc.Handle.DataViewIndex];
			
			if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
			{
				// Required items must have valid pointer and expected type.  
				if (!DataView.IsValid() || !DataView.GetStruct()->IsChildOf(DataDesc.Struct))
				{
					bResult = false;
					break;
				}
			}
			else
			{
				// Optional items must have same type if they are set.
				if (DataView.IsValid() && !DataView.GetStruct()->IsChildOf(DataDesc.Struct))
				{
					bResult = false;
					break;
				}
			 
			}
		}
		return bResult;
	}

	/** @return Handle to external data of type InStruct, or invalid handle if struct not found. */ 
	FStateTreeExternalDataHandle GetExternalDataHandleByStruct(const UStruct* InStruct) const
	{
		check(StateTree);
		const FStateTreeExternalDataDesc* DataDesc = StateTree->ExternalDataDescs.FindByPredicate([InStruct](const FStateTreeExternalDataDesc& Item) { return Item.Struct == InStruct; });
		return DataDesc != nullptr ? DataDesc->Handle : FStateTreeExternalDataHandle::Invalid;
	}

	/** Sets external data view value for specific item. */ 
	void SetExternalData(const FStateTreeExternalDataHandle Handle, FStateTreeDataView DataView)
	{
		check(StateTree);
		check(Handle.IsValid());
		DataViews[Handle.DataViewIndex] = DataView;
	}

	/**
	 * Returns reference to external data based on provided handle. The return type is deduced from the handle's template type.
     * @param Handle Valid TStateTreeExternalDataHandle<> handle. 
	 * @return reference to external data based on handle or null if data is not set.
	 */ 
	template <typename T>
	typename T::DataType& GetExternalData(const T Handle) const
	{
		check(StateTree);
		check(Handle.IsValid());
		checkSlow(StateTree->ExternalDataDescs[Handle.DataViewIndex - StateTree->ExternalDataBaseIndex].Requirement != EStateTreeExternalDataRequirement::Optional); // Optionals should query pointer instead.
		return DataViews[Handle.DataViewIndex].template GetMutable<typename T::DataType>();
	}

	/**
	 * Returns pointer to external data based on provided item handle. The return type is deduced from the handle's template type.
     * @param Handle Valid TStateTreeExternalDataHandle<> handle.
	 * @return pointer to external data based on handle or null if item is not set or handle is invalid.
	 */ 
	template <typename T>
	typename T::DataType* GetExternalDataPtr(const T Handle) const
	{
		check(StateTree);
		return Handle.IsValid() ? DataViews[Handle.DataViewIndex].template GetMutablePtr<typename T::DataType>() : nullptr;
	}

	FStateTreeDataView GetExternalDataView(const FStateTreeExternalDataHandle Handle)
	{
		check(StateTree);
		if (Handle.IsValid())
		{
			return DataViews[Handle.DataViewIndex];
		}
		return FStateTreeDataView();
	}

	/**
	 * Returns reference to instance data property based on provided handle. The return type is deduced from the handle's template type.
	 * @param Handle Valid FStateTreeInstanceDataPropertyHandle<> handle.
	 * @return reference to instance data property based on handle.
	 */ 
	template <typename T>
	typename T::DataType& GetInstanceData(const T Handle) const
	{
		check(StateTree);
		check(Handle.IsValid());
		return *(typename T::DataType*)(DataViews[Handle.DataViewIndex].GetMemory() + Handle.PropertyOffset);
	}

	/**
	 * Returns pointer to instance data property based on provided handle. The return type is deduced from the handle's template type.
	 * @param Handle Valid FStateTreeInstanceDataPropertyHandle<> handle.
	 * @return pointer to instance data property based on handle or null if item is not set or handle is invalid.
	 */ 
	template <typename T>
	typename T::DataType* GetInstanceDataPtr(const T Handle) const
	{
		check(StateTree);
		return Handle.IsValid() ? (typename T::DataType*)(DataViews[Handle.DataViewIndex].GetMemory() + Handle.PropertyOffset) : nullptr;
	}

	/**
	 * Used internally by the Blueprint wrappers to get wrapped instance objects. 
	 * @param DataViewIndex Index to a data view
	 * @return Pointer to an instance object based.
	 */
	template <typename T>
	T* GetInstanceObjectInternal(const int32 DataViewIndex) const
	{
		const UStruct* Struct = DataViews[DataViewIndex].GetStruct();
		if (Struct != nullptr && Struct->IsChildOf<T>())
		{
			return DataViews[DataViewIndex].template GetMutablePtr<T>();
		}
		return nullptr;
	}
	
	EStateTreeRunStatus GetLastTickStatus(FStateTreeDataView ExternalStorage = FStateTreeDataView()) const;

#if WITH_GAMEPLAY_DEBUGGER
	/** @return Debug string describing the current state of the execution */
	FString GetDebugInfoString(FStateTreeDataView ExternalStorage = FStateTreeDataView()) const;
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_STATETREE_DEBUG
	FString GetActiveStateName(FStateTreeDataView ExternalStorage = FStateTreeDataView()) const;
	
	void DebugPrintInternalLayout(FStateTreeDataView ExternalStorage);
#endif

	void AddStructReferencedObjects(class FReferenceCollector& Collector);
	void AddStructReferencedObjects(const FStateTreeDataView ExternalStorage, class FReferenceCollector& Collector);

protected:

	/** @return Prefix that will be used by STATETREE_LOG and STATETREE_CLOG, empty by default. */
	virtual FString GetInstanceDescription() const { return TEXT(""); }

	/** Callback when gated transition is triggered. Contexts that are event based can use this to trigger a future event. */
	virtual void BeginGatedTransition(const FStateTreeExecutionState& Exec) {};
	
	/**
	 * Resets the instance to initial empty state. Note: Does not call ExitState().
	 */
	void Reset();

	/**
	 * Handles logic for entering State. EnterState is called on new active Evaluators and Tasks that are part of the re-planned tree.
	 * Re-planned tree is from the transition target up to the leaf state. States that are parent to the transition target state
	 * and still active after the transition will remain intact.
	 * @return Run status returned by the tasks.
	 */
	EStateTreeRunStatus EnterState(FStateTreeDataView Storage, const FStateTreeTransitionResult& Transition);

	/**
	 * Handles logic for exiting State. ExitState is called on current active Evaluators and Tasks that are part of the re-planned tree.
	 * Re-planned tree is from the transition target up to the leaf state. States that are parent to the transition target state
	 * and still active after the transition will remain intact.
	 */
	void ExitState(FStateTreeDataView Storage, const FStateTreeTransitionResult& Transition);

	/**
	 * Handles logic for exiting State. ExitState is called on current active Evaluators and Tasks in reverse order (from leaf to root).
	 */
	void StateCompleted(FStateTreeDataView Storage, const FStateTreeHandle CurrentState, const EStateTreeRunStatus CompletionStatus);

	/**
	 * Ticks evaluators of all active states starting from current state by delta time.
	 * If TickEvaluators() is called multiple times per frame (i.e. during selection when visiting new states), each state and evaluator is ticked only once.
	 */
	void TickEvaluators(FStateTreeDataView Storage, const FStateTreeHandle CurrentState, const EStateTreeEvaluationType EvalType, const float DeltaTime);

	/**
	 * Ticks tasks of all active states starting from current state by delta time.
	 * @return Run status returned by the tasks.
	 */
	EStateTreeRunStatus TickTasks(FStateTreeDataView Storage, const FStateTreeHandle CurrentState, const float DeltaTime);

	/**
	 * Checks all conditions at given range
	 * @return True if all conditions pass.
	 */
	bool TestAllConditions(FStateTreeDataView Storage, const uint32 ConditionsOffset, const uint32 ConditionsNum);

	/**
	 * Triggers transitions based on current run status. CurrentStatus is used to select which transitions events are triggered.
	 * If CurrentStatus is "Running", "Conditional" transitions pass, "Completed/Failed" will trigger "OnCompleted/OnSucceeded/OnFailed" transitions.
	 * Transition target state can point to a selector state. For that reason the result contains both the target state, as well ass
	 * the actual next state returned by the selector.
	 * @return Transition result describing the source state, state transitioned to, and next selected state.
	 */
	FStateTreeTransitionResult TriggerTransitions(FStateTreeDataView Storage, const FStateTreeStateStatus CurrentStatus, const int Depth);

	/**
	 * Runs state selection logic starting at the specified state, walking towards the leaf states.
	 * If the preconditions of NextState are not met, "Invalid" is returned. 
	 * If NextState is a selector state, SelectState is called recursively (depth-first) to all child states (where NextState will be one of child states).
	 * If NextState is a leaf state, the NextState is returned.
	 * @param Storage View representing all instance data used by tasks and evaluators
	 * @param InitialStateStatus Describes the current state and running status (will be passed intact to next selector)
	 * @param InitialTargetState The state the initial transition target state (will be passed intact to next selector) 
	 * @param NextState The state which we try to select next.
	 * @param Depth Depth of recursion.
	 * @return Transition result describing the source state, transition target state, and next selected state.
	 */
	FStateTreeTransitionResult SelectState(FStateTreeDataView Storage, const FStateTreeStateStatus InitialStateStatus, const FStateTreeHandle InitialTargetState, const FStateTreeHandle NextState, const int Depth);

	/** @return State handles from specified state handle back to the root, specified handle included. */
	int32 GetActiveStates(const FStateTreeHandle StateHandle, TStaticArray<FStateTreeHandle, 32>& OutStateHandles) const;

	/** @return Mutable storage based on storage settings. */
	FStateTreeDataView SelectMutableStorage(FStateTreeDataView ExternalStorage)
	{
		return StorageType == EStateTreeStorage::External ? ExternalStorage : FStateTreeDataView(StorageInstance);
	}

	/** @return Const storage based on storage settings. */
	const FStateTreeDataView SelectStorage(FStateTreeDataView ExternalStorage) const
	{
		return StorageType == EStateTreeStorage::External ? ExternalStorage : FStateTreeDataView(StorageInstance);
	}

	/** @return View to an Evaluator, a Task, or a Condition instance data. */
	FStateTreeDataView GetInstanceData(FStateTreeDataView Storage, const bool bIsObject, const int32 Index) const
	{
		if (UNLIKELY(bIsObject == true))
		{
			const FStateTreeExecutionState& Exec = GetExecState(Storage);
			return FStateTreeDataView(Exec.InstanceObjects[Index]);
		}
		else
		{
			const FStateTreeInstanceStorageOffset& ItemOffset = StateTree->InstanceStorageOffsets[Index];
			return FStateTreeDataView(ItemOffset.Struct, Storage.GetMutableMemory() + ItemOffset.Offset);
		}
	}

	/** @return StateTree execution state from the instance storage. */
	FStateTreeExecutionState& GetExecState(FStateTreeDataView Storage) const
	{
		const FStateTreeInstanceStorageOffset& ItemOffset = StateTree->InstanceStorageOffsets[0];
		check(ItemOffset.Struct == FStateTreeExecutionState::StaticStruct());
		return *reinterpret_cast<FStateTreeExecutionState*>(Storage.GetMutableMemory() + ItemOffset.Offset);
	}

	/** @return Item at specified index. */
	template <typename T>
	T& GetItem(const int32 Index) const
	{
		return StateTree->Items[Index].template GetMutable<T>();
	}
	
	/** @return String describing state status for logging and debug. */
	FString GetStateStatusString(const FStateTreeStateStatus StateStatus) const;

	/** @return String describing state name for logging and debug. */
	FString GetSafeStateName(const FStateTreeHandle State) const;

	/** @return String describing full path of an activate state for logging and debug. */
	FString DebugGetStatePath(TArrayView<FStateTreeHandle> ActiveStateHandles, int32 ActiveStateIndex) const;

	/** The StateTree asset the context is initialized for */
	UPROPERTY()
	const UStateTree* StateTree = nullptr;

	UPROPERTY()
	UObject* Owner = nullptr;
	
	/** States visited during a tick while updating evaluators. Initialized to match the number of states in the asset. */ 
	TArray<bool> VisitedStates;

	/** Array of data pointers (external data, tasks, evaluators, conditions), used during evaluation. Initialized to match the number of items in the asset. */
	TArray<FStateTreeDataView> DataViews;

	/** Optional Instance of the storage */
	FInstancedStruct StorageInstance;

	/** Storage type of the context */
	EStateTreeStorage StorageType = EStateTreeStorage::Internal;
};

template<>
struct TStructOpsTypeTraits<FStateTreeExecutionContext> : public TStructOpsTypeTraitsBase2<FStateTreeExecutionContext>
{
	enum
	{
		WithAddStructReferencedObjects = true,
	};
};
