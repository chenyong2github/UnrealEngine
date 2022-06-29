// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTree.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeInstanceData.h"
#include "StateTreeNodeBase.h"
#include "StateTreeExecutionContext.generated.h"

struct FStateTreeEvaluatorBase;
struct FStateTreeTaskBase;
struct FStateTreeConditionBase;

USTRUCT()
struct STATETREEMODULE_API FStateTreeExecutionState
{
	GENERATED_BODY()

	/** Currently active states */
	FStateTreeActiveStates ActiveStates;

	/** Index of the first task struct in the currently initialized instance data. */
	FStateTreeIndex16 FirstTaskStructIndex = FStateTreeIndex16::Invalid;
	
	/** Index of the first task object in the currently initialized instance data. */
	FStateTreeIndex16 FirstTaskObjectIndex = FStateTreeIndex16::Invalid;

	/** The index of the task that failed during enter state. Exit state uses it to call ExitState() symmetrically. */
	FStateTreeIndex16 EnterStateFailedTaskIndex = FStateTreeIndex16::Invalid;

	/** Result of last tick */
	EStateTreeRunStatus LastTickStatus = EStateTreeRunStatus::Failed;

	/** Running status of the instance */
	EStateTreeRunStatus TreeRunStatus = EStateTreeRunStatus::Unset;

	/** Delayed transition handle, if exists */
	FStateTreeIndex16 GatedTransitionIndex = FStateTreeIndex16::Invalid;

	/** Number of times a new state has been changed. */
	uint16 StateChangeCount = 0;

	/** Running time of the delayed transition */
	float GatedTransitionTime = 0.0f;
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

	/** Updates data view of the parameters by using the default values defined in the StateTree asset. */
	void SetDefaultParameters();

	/**
	 * Updates data view of the parameters by replacing the default values defined in the StateTree asset by the provided values.
	 * Note: caller is responsible to make sure external parameters lifetime matches the context.
	 */
	void SetParameters(const FInstancedPropertyBag& Parameters);
	
	/** Resets the instance to initial empty state. Note: Does not call ExitState(). */
	void Reset();

	/** Returns the StateTree asset in use. */
	const UStateTree* GetStateTree() const { return StateTree; }

	/** @return The owner of the context */
	UObject* GetOwner() const { return Owner; }
	/** @return The world of the owner or nullptr if the owner is not set. */ 
	UWorld* GetWorld() const { return Owner ? Owner->GetWorld() : nullptr; };

	/** @return True of the the execution context is valid and initialized. */ 
	bool IsValid() const { return Owner != nullptr && StateTree != nullptr; }
	
	/** Start executing. */
	EStateTreeRunStatus Start(FStateTreeInstanceData* ExternalInstanceData = nullptr);
	/** Stop executing. */
	EStateTreeRunStatus Stop(FStateTreeInstanceData* ExternalInstanceData = nullptr);

	/** Tick the state tree logic. */
	EStateTreeRunStatus Tick(const float DeltaTime, FStateTreeInstanceData* ExternalInstanceData = nullptr);

	/** @return Pointer to a State or null if state not found */ 
	const FCompactStateTreeState* GetStateFromHandle(const FStateTreeStateHandle StateHandle) const
	{
		return (StateTree && StateTree->States.IsValidIndex(StateHandle.Index)) ? &StateTree->States[StateHandle.Index] : nullptr;
	}

	/** @return Array view to external data descriptors associated with this context. Note: Init() must be called before calling this method. */
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const
	{
		check(StateTree);
		return StateTree->ExternalDataDescs;
	}

	/** @return Array view to named external data descriptors associated with this context. Note: Init() must be called before calling this method. */
	TConstArrayView<FStateTreeExternalDataDesc> GetNamedExternalDataDescs() const
	{
		check(StateTree);
		return StateTree->GetNamedExternalDataDescs();
	}

	/** @return True if all required external data pointers are set. */ 
	bool AreExternalDataViewsValid() const
	{
		check(StateTree);
		bool bResult = true;
		for (const FStateTreeExternalDataDesc& DataDesc : StateTree->ExternalDataDescs)
		{
			const FStateTreeDataView& DataView = DataViews[DataDesc.Handle.DataViewIndex.Get()];
			
			if (DataDesc.Requirement == EStateTreeExternalDataRequirement::Required)
			{
				// Required items must have valid pointer of the expected type.  
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

		for (const FStateTreeExternalDataDesc& DataDesc : StateTree->GetNamedExternalDataDescs())
		{
			const FStateTreeDataView& DataView = DataViews[DataDesc.Handle.DataViewIndex.Get()];

			// Items must have valid pointer of the expected type.  
			if (!DataView.IsValid() || !DataView.GetStruct()->IsChildOf(DataDesc.Struct))
			{
				bResult = false;
				break;
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
		DataViews[Handle.DataViewIndex.Get()] = DataView;
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
		checkSlow(StateTree->ExternalDataDescs[Handle.DataViewIndex.Get() - StateTree->ExternalDataBaseIndex].Requirement != EStateTreeExternalDataRequirement::Optional); // Optionals should query pointer instead.
		return DataViews[Handle.DataViewIndex.Get()].template GetMutable<typename T::DataType>();
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
		return Handle.IsValid() ? DataViews[Handle.DataViewIndex.Get()].template GetMutablePtr<typename T::DataType>() : nullptr;
	}

	FStateTreeDataView GetExternalDataView(const FStateTreeExternalDataHandle Handle)
	{
		check(StateTree);
		if (Handle.IsValid())
		{
			return DataViews[Handle.DataViewIndex.Get()];
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
		return *(typename T::DataType*)(DataViews[Handle.DataViewIndex.Get()].GetMemory() + Handle.PropertyOffset);
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
		return Handle.IsValid() ? (typename T::DataType*)(DataViews[Handle.DataViewIndex.Get()].GetMemory() + Handle.PropertyOffset) : nullptr;
	}

	/**
	 * Used internally by the Blueprint wrappers to get wrapped instance objects. 
	 * @param DataViewIndex Index to a data view
	 * @return Pointer to an instance object based.
	 */
	template <typename T>
	T* GetInstanceObjectInternal(const FStateTreeIndex16 DataViewIndex) const
	{
		const UStruct* Struct = DataViews[DataViewIndex.Get()].GetStruct();
		if (Struct != nullptr && Struct->IsChildOf<T>())
		{
			return DataViews[DataViewIndex.Get()].template GetMutablePtr<T>();
		}
		return nullptr;
	}

	/** @returns pointer to the instance data of specified node. */
	template <typename T>
	T* GetInstanceDataPtr(const FStateTreeNodeBase& Node) const
	{
		return DataViews[Node.DataViewIndex.Get()].template GetMutablePtr<T>();
	}

	/** @returns reference to the instance data of specified node. */
	template <typename T>
	T& GetInstanceData(const FStateTreeNodeBase& Node) const
	{
		return DataViews[Node.DataViewIndex.Get()].template GetMutable<T>();
	}

	/** @return the status of the last tick function */
	EStateTreeRunStatus GetLastTickStatus(const FStateTreeInstanceData* ExternalInstanceData = nullptr) const;

	/** @return reference to the list of currently active states. */
	const FStateTreeActiveStates& GetActiveStates(const FStateTreeInstanceData* ExternalInstanceData = nullptr) const;

#if WITH_GAMEPLAY_DEBUGGER
	/** @return Debug string describing the current state of the execution */
	FString GetDebugInfoString(const FStateTreeInstanceData* ExternalInstanceData = nullptr) const;
#endif // WITH_GAMEPLAY_DEBUGGER

#if WITH_STATETREE_DEBUG
	int32 GetStateChangeCount(const FStateTreeInstanceData* ExternalInstanceData = nullptr) const;

	void DebugPrintInternalLayout(const FStateTreeInstanceData* ExternalInstanceData = nullptr);
#endif

	FString GetActiveStateName(const FStateTreeInstanceData* ExternalInstanceData = nullptr) const;
	TArray<FName> GetActiveStateNames(const FStateTreeInstanceData* ExternalInstanceData = nullptr) const;

protected:

	/** @return Prefix that will be used by STATETREE_LOG and STATETREE_CLOG, empty by default. */
	virtual FString GetInstanceDescription() const;

	/** Callback when gated transition is triggered. Contexts that are event based can use this to trigger a future event. */
	virtual void BeginGatedTransition(const FStateTreeExecutionState& Exec) {};

	void UpdateInstanceData(FStateTreeInstanceData& InstanceData, const FStateTreeActiveStates& CurrentActiveStates, const FStateTreeActiveStates& NextActiveStates);

	/**
	 * Handles logic for entering State. EnterState is called on new active Evaluators and Tasks that are part of the re-planned tree.
	 * Re-planned tree is from the transition target up to the leaf state. States that are parent to the transition target state
	 * and still active after the transition will remain intact.
	 * @return Run status returned by the tasks.
	 */
	EStateTreeRunStatus EnterState(FStateTreeInstanceData& InstanceData, const FStateTreeTransitionResult& Transition);

	/**
	 * Handles logic for exiting State. ExitState is called on current active Evaluators and Tasks that are part of the re-planned tree.
	 * Re-planned tree is from the transition target up to the leaf state. States that are parent to the transition target state
	 * and still active after the transition will remain intact.
	 */
	void ExitState(FStateTreeInstanceData& InstanceData, const FStateTreeTransitionResult& Transition);

	/**
	 * Handles logic for signalling State completed. StateCompleted is called on current active Evaluators and Tasks in reverse order (from leaf to root).
	 */
	void StateCompleted(FStateTreeInstanceData& InstanceData);

	/**
	 * Ticks global evaluators by delta time.
	 */
	void TickEvaluators(FStateTreeInstanceData& InstanceData, const float DeltaTime);

	void StartEvaluators(FStateTreeInstanceData& InstanceData);

	void StopEvaluators(FStateTreeInstanceData& InstanceData);

	/**
	 * Ticks tasks of all active states starting from current state by delta time.
	 * @return Run status returned by the tasks.
	 */
	EStateTreeRunStatus TickTasks(FStateTreeInstanceData& InstanceData, const float DeltaTime);

	/**
	 * Checks all conditions at given range
	 * @return True if all conditions pass.
	 */
	bool TestAllConditions(const int32 ConditionsOffset, const int32 ConditionsNum);

	/**
	 * Triggers transitions based on current run status. CurrentStatus is used to select which transitions events are triggered.
	 * If CurrentStatus is "Running", "Conditional" transitions pass, "Completed/Failed" will trigger "OnCompleted/OnSucceeded/OnFailed" transitions.
	 * Transition target state can point to a selector state. For that reason the result contains both the target state, as well ass
	 * the actual next state returned by the selector.
	 * @return Transition result describing the source state, state transitioned to, and next selected state.
	 */
	bool TriggerTransitions(FStateTreeInstanceData& InstanceData, FStateTreeTransitionResult& OutTransition);

	/**
	 * Runs state selection logic starting at the specified state, walking towards the leaf states.
	 * If a state cannot be selected, false is returned. 
	 * If NextState is a selector state, SelectStateInternal is called recursively (depth-first) to all child states (where NextState will be one of child states).
	 * If NextState is a leaf state, the active states leading from root to the leaf are returned.
	 * @param InstanceData Reference to the instance data
	 * @param NextState The state which we try to select next.
	 * @param OutNewActiveStates Active states that got selected.
	 * @return True if succeeded to select new active states.
	 */
	bool SelectState(FStateTreeInstanceData& InstanceData, const FStateTreeStateHandle NextState, FStateTreeActiveStates& OutNewActiveStates);

	/**
	 * Used internally to do the recursive part of the SelectState().
	 */
	bool SelectStateInternal(FStateTreeInstanceData& InstanceData, const FStateTreeStateHandle NextState, FStateTreeActiveStates& OutNewActiveStates);

	/** @return Mutable storage based on storage settings. */
	FStateTreeInstanceData& SelectMutableInstanceData(FStateTreeInstanceData* ExternalInstanceData)
	{
		check(StorageType != EStateTreeStorage::External || (StorageType == EStateTreeStorage::External && ExternalInstanceData != nullptr));
		return StorageType == EStateTreeStorage::External ? *ExternalInstanceData : InternalInstanceData;
	}

	/** @return Const storage based on storage settings. */
	const FStateTreeInstanceData& SelectInstanceData(const FStateTreeInstanceData* ExternalInstanceData) const
	{
		check(StorageType != EStateTreeStorage::External || (StorageType == EStateTreeStorage::External && ExternalInstanceData != nullptr));
		return StorageType == EStateTreeStorage::External ? *ExternalInstanceData : InternalInstanceData;
	}

	/** @return StateTree execution state from the instance storage. */
	static FStateTreeExecutionState& GetExecState(FStateTreeInstanceData& InstanceData)
	{
		return InstanceData.GetMutableStruct(0).GetMutable<FStateTreeExecutionState>();
	}

	/** @return const StateTree execution state from the instance storage. */
	static const FStateTreeExecutionState& GetExecState(const FStateTreeInstanceData& InstanceData)
	{
		return InstanceData.GetStruct(0).Get<FStateTreeExecutionState>();
	}

	/** Sets up parameter data view for a linked state and copies bound properties. */
	void UpdateLinkedStateParameters(const FStateTreeInstanceData& InstanceData, const FCompactStateTreeState& State, const uint16 ParameterInstanceIndex);

	/** Sets up parameter data view for subtree state. */
	void UpdateSubtreeStateParameters(const FStateTreeInstanceData& InstanceData, const FCompactStateTreeState& State);
	
	/** @return String describing state status for logging and debug. */
	FString GetStateStatusString(const FStateTreeExecutionState& ExecState) const;

	/** @return String describing state name for logging and debug. */
	FString GetSafeStateName(const FStateTreeStateHandle State) const;

	/** @return String describing full path of an activate state for logging and debug. */
	FString DebugGetStatePath(const FStateTreeActiveStates& ActiveStates, int32 ActiveStateIndex) const;

	/** The StateTree asset the context is initialized for */
	UPROPERTY(Transient)
	TObjectPtr<const UStateTree> StateTree = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UObject> Owner = nullptr;
	
	/** Optional Instance of the storage */
	UPROPERTY(Transient)
	FStateTreeInstanceData InternalInstanceData;

	/** Array of data pointers (external data, tasks, evaluators, conditions), used during evaluation. Initialized to match the number of items in the asset. */
	TArray<FStateTreeDataView> DataViews;

	/** Storage type of the context */
	EStateTreeStorage StorageType = EStateTreeStorage::Internal;
};
