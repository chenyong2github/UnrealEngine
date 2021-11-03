// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.generated.h"

STATETREEMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogStateTree, Warning, All);

#ifndef WITH_STATETREE_DEBUG
#define WITH_STATETREE_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)
#endif // WITH_STATETREE_DEBUG

/** Types of supported variables */
UENUM()
enum class EStateTreeVariableType : uint8
{
	Void,
	Float,
	Int,
	Bool,
	Vector,
	Object,
};

/**
 * Status describing current ticking state. 
 */
UENUM()
enum class EStateTreeRunStatus : uint8
{
	Unset,				/** Status not set. */
	Failed,				/** Tree execution has stopped on failure. */
	Succeeded,			/** Tree execution has stopped on success. */
	Running,			/** Tree is still running. */
};

/**  Evaluator evaluation type. */
UENUM()
enum class EStateTreeEvaluationType : uint8
{
	PreSelect,			/** Called during selection process on states that have not been visited yet. */
    Tick,				/** Called during tick on active states. */
};

/**  State change type. Passed to EnterState() and ExitState() to indicate how the state change affects the state and Evaluator or Task is on. */
UENUM()
enum class EStateTreeStateChangeType : uint8
{
	None,				/** Not an activation */
	Changed,			/** The state became activated or deactivated. */
    Sustained,			/** The state is parent of new active state and sustained previous active state. */
};

/** Transitions behavior. */
UENUM()
enum class EStateTreeTransitionType : uint8
{
	Succeeded,			// Signal StateTree execution succeeded.
	Failed,				// Signal StateTree execution failed.
	GotoState,			// Transition to specified state.
	NotSet,				// No transition.
	NextState,			// Goto next sibling state.
};


/** Transitions event. */
UENUM()
enum class EStateTreeTransitionEvent : uint8
{
	None = 0 UMETA(Hidden),
	OnCompleted = 0x1 | 0x2,
    OnSucceeded = 0x1,
    OnFailed = 0x2,
    OnCondition = 0x4,
};
ENUM_CLASS_FLAGS(EStateTreeTransitionEvent)

/**
 * Handle that is used to refer baked state tree data.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeHandle
{
	GENERATED_BODY()

	static const uint16 InvalidIndex = uint16(-1);		// Index value indicating invalid item.
	static const uint16 SucceededIndex = uint16(-2);	// Index value indicating a Succeeded item.
	static const uint16 FailedIndex = uint16(-3);		// Index value indicating a Failed item.
	
	static const FStateTreeHandle Invalid;
	static const FStateTreeHandle Succeeded;
	static const FStateTreeHandle Failed;

	FStateTreeHandle() = default;
	explicit FStateTreeHandle(uint16 InIndex) : Index(InIndex) {}

	bool IsValid() const { return Index != InvalidIndex; }

	bool operator==(const FStateTreeHandle& RHS) const { return Index == RHS.Index; }
	bool operator!=(const FStateTreeHandle& RHS) const { return Index != RHS.Index; }

	FString Describe() const
	{
		switch (Index)
		{
		case InvalidIndex:		return TEXT("Invalid Item");
		case SucceededIndex: 	return TEXT("Succeeded Item");
		case FailedIndex: 		return TEXT("Failed Item");
		default: 				return FString::Printf(TEXT("%d"), Index);
		}
	}

	UPROPERTY()
	uint16 Index = InvalidIndex;
};

UENUM()
enum class EStateTreeResultStatus : uint8
{
	Unset,
	Available,
    InUse,
	Succeeded,
    Failed,
};

USTRUCT()
struct STATETREEMODULE_API FStateTreeResult
{
	GENERATED_BODY()
	virtual ~FStateTreeResult() {}
	virtual const UScriptStruct& GetStruct() const PURE_VIRTUAL(FStateTreeResult::GetStruct, return *FStateTreeResult::StaticStruct(); );
};
template<> struct TStructOpsTypeTraits<FStateTreeResult> : public TStructOpsTypeTraitsBase2<FStateTreeResult> { enum { WithPureVirtual = true, }; };

USTRUCT()
struct STATETREEMODULE_API FStateTreeResultRef
{
	GENERATED_BODY()

	FStateTreeResultRef() = default;
	explicit FStateTreeResultRef(FStateTreeResult* InResult) : Result(InResult) {}
	FStateTreeResultRef(const FStateTreeResultRef& InOther) : FStateTreeResultRef(InOther.Result) {}
	~FStateTreeResultRef()
	{
		Result = nullptr;
	}

	FStateTreeResultRef& operator=(FStateTreeResult* InResult)
	{
		Result = InResult;
		return *this;
	}

	FStateTreeResultRef& operator=(const FStateTreeResultRef& InOther)
	{
		Result = InOther.Result;
		return *this;
	}

	bool IsValid() const { return Result != nullptr; }

	void Reset()
	{
		Result = nullptr;
	}

	template<typename T>
	bool IsA() const
	{
		const UScriptStruct* ScriptStruct = Result != nullptr ? &Result->GetStruct() : nullptr;
		return ScriptStruct != nullptr && ScriptStruct->IsChildOf(T::StaticStruct());
	}
	
	// Returns mutable reference to the struct, this getter assumes that all data is valid
	template<typename T>
    T& GetMutable()
	{
		check(Result != nullptr);
		const UScriptStruct& ScriptStruct = Result->GetStruct();
		check(ScriptStruct.IsChildOf(T::StaticStruct()));
		return *static_cast<T*>(Result);
	}

	// Returns const reference to the struct, this getter assumes that all data is valid
	template<typename T>
    const T& Get() const
	{
		check(Result != nullptr);
		const UScriptStruct& ScriptStruct = Result->GetStruct();
		check(ScriptStruct.IsChildOf(T::StaticStruct()));
		return *static_cast<T*>(Result);
	}

	// Returns mutable pointer to the struct, or nullptr if cast is not valid.
	template<typename T>
    T* GetMutablePtr()
	{
		const UScriptStruct* ScriptStruct = Result != nullptr ? &Result->GetStruct() : nullptr;
		if (ScriptStruct != nullptr && ScriptStruct->IsChildOf(T::StaticStruct()))
		{
			return static_cast<T*>(Result);
		}
		return nullptr;
	}

	// Returns const pointer to the struct, or nullptr if cast is not valid.
	template<typename T>
    const T* GetPtr() const
	{
		const UScriptStruct* ScriptStruct = Result != nullptr ? &Result->GetStruct() : nullptr;
		if (ScriptStruct != nullptr && ScriptStruct->IsChildOf(T::StaticStruct()))
		{
			return static_cast<T*>(Result);
		}
		return nullptr;
	}
	
private:
	FStateTreeResult* Result = nullptr;
};


/**
 * Describes current status of a running state or desired state.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeStateStatus
{
	GENERATED_BODY()

	FStateTreeStateStatus() = default;
	FStateTreeStateStatus(const FStateTreeHandle InState, const EStateTreeRunStatus InStatus) : State(InState), RunStatus(InStatus) {}
	FStateTreeStateStatus(const FStateTreeHandle InState) : State(InState), RunStatus(EStateTreeRunStatus::Running) {}
	FStateTreeStateStatus(const EStateTreeRunStatus InStatus) : State(), RunStatus(InStatus) {}

	bool IsSet() const { return RunStatus != EStateTreeRunStatus::Unset; }
	
	UPROPERTY()
	FStateTreeHandle State = FStateTreeHandle::Invalid;

	UPROPERTY()
	EStateTreeRunStatus RunStatus = EStateTreeRunStatus::Unset;
};

/**
 * Describes a state tree transition. Source is the state where the transition started, Target describes the state where the transition pointed at,
 * and Next describes the selected state. The reason Transition and Next are different is that Transition state can be a selector state,
 * in which case the children will be visited until a leaf state is found, which will be the next state.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeTransitionResult
{
	GENERATED_BODY()

	FStateTreeTransitionResult() = default;
	FStateTreeTransitionResult(const FStateTreeStateStatus InSource, const FStateTreeHandle InTransitionAndNext) : Source(InSource), Target(InTransitionAndNext), Next(InTransitionAndNext) {}
	FStateTreeTransitionResult(const FStateTreeStateStatus InSource, const FStateTreeHandle InTransition, const FStateTreeHandle InNext) : Source(InSource), Target(InTransition), Next(InNext) {}

	/** State where the transition started. */
	UPROPERTY()
	FStateTreeStateStatus Source = FStateTreeStateStatus();

	/** Transition target state */
	UPROPERTY()
	FStateTreeHandle Target = FStateTreeHandle::Invalid;

	/** Selected state, can be different from Transition, if Transition is a selector state. */
	UPROPERTY()
	FStateTreeHandle Next = FStateTreeHandle::Invalid;

	/** Current state, update as we execute the tree. */
	UPROPERTY()
	FStateTreeHandle Current = FStateTreeHandle::Invalid;
};


/**
 *  Runtime representation of a StateTree transition.
 */
USTRUCT()
struct STATETREEMODULE_API FBakedStateTransition
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 ConditionsBegin = 0;							// Index to first condition to test
	UPROPERTY()
	FStateTreeHandle State = FStateTreeHandle::Invalid;	// Target state of the transition
	UPROPERTY()
	EStateTreeTransitionType Type = EStateTreeTransitionType::NotSet;	// Type of the transition.
	UPROPERTY()
	EStateTreeTransitionEvent Event = EStateTreeTransitionEvent::None;	// Type of the transition event.
	UPROPERTY()
	uint8 GateDelay = 0;								// The time the conditions need to hold true for the transition to become active, in tenths of a seconds.
	UPROPERTY()
	uint8 ConditionsNum = 0;							// Number of conditions to test.
};

/**
 *  Runtime representation of a StateTree state.
 */
USTRUCT()
struct STATETREEMODULE_API FBakedStateTreeState
{
	GENERATED_BODY()

	/** @return Index to the next sibling state. */
	uint16 GetNextSibling() const { return ChildrenEnd; }

	/** @return True if the state has any child states */
	bool HasChildren() const { return ChildrenEnd > ChildrenBegin; }

	UPROPERTY()
	FName Name;											// Name of the State

	UPROPERTY()
	FStateTreeHandle Parent = FStateTreeHandle::Invalid;	// Parent state
	UPROPERTY()
	uint16 ChildrenBegin = 0;							// Index to first child state
	UPROPERTY()
	uint16 ChildrenEnd = 0;								// Index one past the last child state

	UPROPERTY()
	FStateTreeHandle StateDoneTransitionState = FStateTreeHandle::Invalid;		// State to transition to when the state execution is done. See also StateDoneTransitionType.
	UPROPERTY()
	FStateTreeHandle StateFailedTransitionState = FStateTreeHandle::Invalid;	// State to transition to if the state execution fails. See also StateFailedTransitionType.
	
	UPROPERTY()
	uint16 EnterConditionsBegin = 0;					// Index to first state enter condition
	UPROPERTY()
	uint16 TransitionsBegin = 0;						// Index to first transition
	UPROPERTY()
	uint16 TasksBegin = 0;								// Index to first task
	UPROPERTY()
	uint16 EvaluatorsBegin = 0;							// Index to first evaluator

	UPROPERTY()
	uint8 EnterConditionsNum = 0;						// Number of enter conditions
	UPROPERTY()
	uint8 TransitionsNum = 0;							// Number of transitions
	UPROPERTY()
	uint8 TasksNum = 0;									// Number of tasks
	UPROPERTY()
	uint8 EvaluatorsNum = 0;							// Number of evaluators

	UPROPERTY()
	EStateTreeTransitionType StateDoneTransitionType = EStateTreeTransitionType::NotSet;		// Type of the State Done transition. See also StateDoneTransitionState.
	UPROPERTY()
	EStateTreeTransitionType StateFailedTransitionType = EStateTreeTransitionType::NotSet;		// Type of the State Failed transition. See also StateFailedTransitionState.
};

/** An offset into the StateTree runtime storage type to get a struct view to a specific Task or Evaluator. */
struct FStateTreeRuntimeStorageItemOffset
{
	FStateTreeRuntimeStorageItemOffset() = default;
	FStateTreeRuntimeStorageItemOffset(const UScriptStruct* InStruct, const int32 InOffset) : Struct(InStruct), Offset(InOffset) {}

	/** Struct of the item */
	const UScriptStruct* Struct = nullptr;
	/** Offset within the storage struct */
	int32 Offset = 0;
};

UENUM()
enum class EStateTreeItemRequirement : uint8
{
	Required,	// StateTree cannot be executed if the item is not present.
	Optional,	// Item is optional for StateTree execution.
};

/**
 * Handle to access an external struct or object.
 * Note: Use the templated version below. 
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeItemHandle
{
	GENERATED_BODY()

	static const FStateTreeItemHandle Invalid;
	static constexpr uint8 IndexNone = MAX_uint8;

	static bool IsValidIndex(const int32 Index) { return Index >= 0 && Index < (int32)IndexNone; }

	bool IsValid() const { return ItemIndex != IndexNone; }
	
	uint8 ItemIndex = IndexNone;
};

/**
 * Handle to access an external struct or object.
 * This reference handle can be used in StateTree tasks and evaluators to have quick access to external items.
 * The type provided to the template is used by the linker and context to pass along the type.
 *
 * USTRUCT()
 * struct FExampleTask : public FStateTreeTaskBase
 * {
 *    ...
 *
 *    bool Link(FStateTreeLinker& Linker)
 *    {
 *      Linker.LinkExternalItem(ExampleSubsystemHandle);
 *      return true;
 *    }
 * 
 *    EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
 *    {
 *      const UExampleSubsystem& ExampleSubsystem = Context.GetExternalItem(ExampleSubsystemHandle);
 *      ...
 *    }
 *
 *    TStateTreeItemHandle<UExampleSubsystem> ExampleSubsystemHandle;
 * }
 */
template<typename T, EStateTreeItemRequirement Req = EStateTreeItemRequirement::Required>
struct TStateTreeItemHandle : FStateTreeItemHandle
{
	typedef T ItemType;
	static constexpr EStateTreeItemRequirement ItemRequirement = Req;
};

/**
 * Describes an external item. The item can point to a struct or object.
 * The code that handles StateTree ticking is responsible for passing in the actually data, see FStateTreeExecutionContext.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeExternalItemDesc
{
	GENERATED_BODY()

	FStateTreeExternalItemDesc() = default;
	FStateTreeExternalItemDesc(const UStruct* InStruct, const EStateTreeItemRequirement InRequirement) : Struct(InStruct), Requirement(InRequirement) {}

	bool operator==(const FStateTreeExternalItemDesc& Other) const
	{
		return Struct == Other.Struct && Requirement == Other.Requirement;
	}
	
	/** Class or struct of the external item. */
	UPROPERTY();
	const UStruct* Struct = nullptr;

	/** Handle/Index to the StateTreeExecutionContext item views array */
	UPROPERTY();
	FStateTreeItemHandle Handle;

	/** Describes if the item is required or not. */
	UPROPERTY();
	EStateTreeItemRequirement Requirement = EStateTreeItemRequirement::Required;
};

/**
 * The StateTree linker is used to resolved references to various StateTree data at load time.
 * @see TStateTreeItemHandle<> for example usage.
 */
struct FStateTreeLinker
{
	/** Sets base index for all external item handles. */
	void SetItemBaseIndex(const int32 InItemBaseIndex) { ItemBaseIndex = InItemBaseIndex; }
	
	/**
	 * Links reference to an external UObject.
	 * @param Handle Reference to TStateTreeItemHandle<> with UOBJECT type to link to.
	 */
	template <typename T>
	typename TEnableIf<TIsDerivedFrom<typename T::ItemType, UObject>::IsDerived, void>::Type LinkExternalItem(T& Handle)
	{
		LinkExternalItem(Handle, T::ItemType::StaticClass(), T::ItemRequirement);
	}

	/**
	 * Links reference to an external UObject.
	 * @param Handle Reference to TStateTreeItemHandle<> with USTRUCT type to link to.
	 */
	template <typename T>
	typename TEnableIf<!TIsDerivedFrom<typename T::ItemType, UObject>::IsDerived, void>::Type LinkExternalItem(T& Handle)
	{
		LinkExternalItem(Handle, T::ItemType::StaticStruct(), T::ItemRequirement);
	}

	/** @return linked external item descriptors. */
	TConstArrayView<FStateTreeExternalItemDesc> GetItemDescs() const { return ItemDescs; }

protected:
	void LinkExternalItem(FStateTreeItemHandle& Handle, const UStruct* Struct, const EStateTreeItemRequirement Requirement)
	{
		const FStateTreeExternalItemDesc Desc(Struct, Requirement);
		int32 Index = ItemDescs.Find(Desc);
		if (Index == INDEX_NONE)
		{
			Index = ItemDescs.Add(Desc);
			check(FStateTreeItemHandle::IsValidIndex(Index + ItemBaseIndex));
			ItemDescs[Index].Handle.ItemIndex = (uint8)(Index + ItemBaseIndex);
		}
		Handle.ItemIndex = (uint8)(Index + ItemBaseIndex);
	}

	int32 ItemBaseIndex = 0;
	TArray<FStateTreeExternalItemDesc> ItemDescs;
};
