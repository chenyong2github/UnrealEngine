// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.generated.h"

STATETREEMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogStateTree, Warning, All);

#ifndef WITH_STATETREE_DEBUG
#define WITH_STATETREE_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)
#endif // WITH_STATETREE_DEBUG

/**
 * Status describing current ticking state. 
 */
UENUM()
enum class EStateTreeRunStatus : uint8
{
	Running,			/** Tree is still running. */
	Failed,				/** Tree execution has stopped on failure. */
	Succeeded,			/** Tree execution has stopped on success. */
	Unset,				/** Status not set. */
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
USTRUCT(BlueprintType)
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


/**
 * Describes current status of a running state or desired state.
 */
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeStateStatus
{
	GENERATED_BODY()

	FStateTreeStateStatus() = default;
	FStateTreeStateStatus(const FStateTreeHandle InState, const EStateTreeRunStatus InStatus) : State(InState), RunStatus(InStatus) {}
	FStateTreeStateStatus(const FStateTreeHandle InState) : State(InState), RunStatus(EStateTreeRunStatus::Running) {}
	FStateTreeStateStatus(const EStateTreeRunStatus InStatus) : State(), RunStatus(InStatus) {}

	bool IsSet() const { return RunStatus != EStateTreeRunStatus::Unset; }
	
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeHandle State = FStateTreeHandle::Invalid;

	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	EStateTreeRunStatus RunStatus = EStateTreeRunStatus::Unset;
};

/**
 * Describes a state tree transition. Source is the state where the transition started, Target describes the state where the transition pointed at,
 * and Next describes the selected state. The reason Transition and Next are different is that Transition state can be a selector state,
 * in which case the children will be visited until a leaf state is found, which will be the next state.
 */
USTRUCT(BlueprintType)
struct STATETREEMODULE_API FStateTreeTransitionResult
{
	GENERATED_BODY()

	FStateTreeTransitionResult() = default;
	FStateTreeTransitionResult(const FStateTreeStateStatus InSource, const FStateTreeHandle InTransitionAndNext) : Source(InSource), Target(InTransitionAndNext), Next(InTransitionAndNext) {}
	FStateTreeTransitionResult(const FStateTreeStateStatus InSource, const FStateTreeHandle InTransition, const FStateTreeHandle InNext) : Source(InSource), Target(InTransition), Next(InNext) {}

	/** State where the transition started. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeStateStatus Source = FStateTreeStateStatus();

	/** Transition target state */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeHandle Target = FStateTreeHandle::Invalid;

	/** Selected state, can be different from Transition, if Transition is a selector state. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
	FStateTreeHandle Next = FStateTreeHandle::Invalid;

	/** Current state, update as we execute the tree. */
	UPROPERTY(EditDefaultsOnly, Category = Default, BlueprintReadOnly)
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

/** An offset into the StateTree runtime storage type to get a struct view to a specific Task, Evaluator, or Condition. */
struct FStateTreeInstanceStorageOffset
{
	FStateTreeInstanceStorageOffset() = default;
	FStateTreeInstanceStorageOffset(const UScriptStruct* InStruct, const int32 InOffset) : Struct(InStruct), Offset(InOffset) {}

	/** Struct of the data the offset points at */
	const UScriptStruct* Struct = nullptr;
	/** Offset within the storage struct */
	int32 Offset = 0;
};

UENUM()
enum class EStateTreeExternalDataRequirement : uint8
{
	Required,	// StateTree cannot be executed if the data is not present.
	Optional,	// Data is optional for StateTree execution.
};

/**
 * Handle to access an external struct or object.
 * Note: Use the templated version below. 
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeExternalDataHandle
{
	GENERATED_BODY()

	static const FStateTreeExternalDataHandle Invalid;
	static constexpr uint8 IndexNone = MAX_uint8;

	static bool IsValidIndex(const int32 Index) { return Index >= 0 && Index < (int32)IndexNone; }

	bool IsValid() const { return DataViewIndex != IndexNone; }
	
	uint8 DataViewIndex = IndexNone;
};

/**
 * Handle to access an external struct or object.
 * This reference handle can be used in StateTree tasks and evaluators to have quick access to external data.
 * The type provided to the template is used by the linker and context to pass along the type.
 *
 * USTRUCT()
 * struct FExampleTask : public FStateTreeTaskBase
 * {
 *    ...
 *
 *    bool Link(FStateTreeLinker& Linker)
 *    {
 *      Linker.LinkExternalData(ExampleSubsystemHandle);
 *      return true;
 *    }
 * 
 *    EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
 *    {
 *      const UExampleSubsystem& ExampleSubsystem = Context.GetExternalData(ExampleSubsystemHandle);
 *      ...
 *    }
 *
 *    TStateTreeExternalDataHandle<UExampleSubsystem> ExampleSubsystemHandle;
 * }
 */
template<typename T, EStateTreeExternalDataRequirement Req = EStateTreeExternalDataRequirement::Required>
struct TStateTreeExternalDataHandle : FStateTreeExternalDataHandle
{
	typedef T DataType;
	static constexpr EStateTreeExternalDataRequirement DataRequirement = Req;
};

UENUM()
enum class EStateTreePropertyIndirection : uint8
{
	Offset,
	Indirect,
};

UENUM()
enum class EStateTreePropertyUsage : uint8
{
	Invalid,
	Input,
	Parameter,
	Output,
	Internal,
};


USTRUCT()
struct STATETREEMODULE_API FStateTreeInstanceDataPropertyHandle
{
	GENERATED_BODY()

	static constexpr uint8 IndexNone = MAX_uint8;

	static bool IsValidIndex(const int32 Index) { return Index >= 0 && Index < (int32)IndexNone; }

	bool IsValid() const { return DataViewIndex != IndexNone; }

	uint16 PropertyOffset = 0;
	uint8 DataViewIndex = IndexNone;
	EStateTreePropertyIndirection Type = EStateTreePropertyIndirection::Offset;
};

template<typename T>
struct TStateTreeInstanceDataPropertyHandle : FStateTreeInstanceDataPropertyHandle
{
	typedef T DataType;
};


/**
 * Describes an external data. The data can point to a struct or object.
 * The code that handles StateTree ticking is responsible for passing in the actually data, see FStateTreeExecutionContext.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeExternalDataDesc
{
	GENERATED_BODY()

	FStateTreeExternalDataDesc() = default;
	FStateTreeExternalDataDesc(const UStruct* InStruct, const EStateTreeExternalDataRequirement InRequirement) : Struct(InStruct), Requirement(InRequirement) {}

	bool operator==(const FStateTreeExternalDataDesc& Other) const
	{
		return Struct == Other.Struct && Requirement == Other.Requirement;
	}
	
	/** Class or struct of the external data. */
	UPROPERTY();
	const UStruct* Struct = nullptr;

	/** Handle/Index to the StateTreeExecutionContext data views array */
	UPROPERTY();
	FStateTreeExternalDataHandle Handle;

	/** Describes if the data is required or not. */
	UPROPERTY();
	EStateTreeExternalDataRequirement Requirement = EStateTreeExternalDataRequirement::Required;
};


#define STATETREE_INSTANCEDATA_PROPERTY(Struct, Member) \
		decltype(Struct::Member){}, Struct::StaticStruct(), TEXT(#Member)

UENUM()
enum class EStateTreeLinkerStatus : uint8
{
	Succeeded,
	Failed,
};
/**
 * The StateTree linker is used to resolved references to various StateTree data at load time.
 * @see TStateTreeExternalDataHandle<> for example usage.
 */
struct FStateTreeLinker
{
	/** Sets base index for all external data handles. */
	void SetExternalDataBaseIndex(const int32 InExternalDataBaseIndex) { ExternalDataBaseIndex = InExternalDataBaseIndex; }

	/** Sets currently linked item's instance data type and index. */ 
	void SetCurrentInstanceDataType(const UStruct* Struct, const int32 Index)
	{
		CurrentInstanceStruct = Struct;
		CurrentInstanceIndex = Index;
	}

	EStateTreeLinkerStatus GetStatus() const { return Status; }
	
	/**
	 * Links reference to an external UObject.
	 * @param Handle Reference to TStateTreeExternalDataHandle<> with UOBJECT type to link to.
	 */
	template <typename T>
	typename TEnableIf<TIsDerivedFrom<typename T::DataType, UObject>::IsDerived, void>::Type LinkExternalData(T& Handle)
	{
		LinkExternalData(Handle, T::DataType::StaticClass(), T::DataRequirement);
	}

	/**
	 * Links reference to an external UObject.
	 * @param Handle Reference to TStateTreeExternalDataHandle<> with USTRUCT type to link to.
	 */
	template <typename T>
	typename TEnableIf<!TIsDerivedFrom<typename T::DataType, UObject>::IsDerived, void>::Type LinkExternalData(T& Handle)
	{
		LinkExternalData(Handle, T::DataType::StaticStruct(), T::DataRequirement);
	}

	/**
	 * Links reference to an external Object or Struct.
	 * This function should only be used when TStateTreeExternalDataHandle<> cannot be used, i.e. the Struct is based on some data.
	 * @param Handle Reference to link to.
	 * @param Struct Expected type of the Object or Struct to link to.
	 * @param Requirement Describes if the external data is expected to be required or optional.
	 */
	void LinkExternalData(FStateTreeExternalDataHandle& Handle, const UStruct* Struct, const EStateTreeExternalDataRequirement Requirement)
	{
		const FStateTreeExternalDataDesc Desc(Struct, Requirement);
		int32 Index = ExternalDataDescs.Find(Desc);
		if (Index == INDEX_NONE)
		{
			Index = ExternalDataDescs.Add(Desc);
			check(FStateTreeExternalDataHandle::IsValidIndex(Index + ExternalDataBaseIndex));
			ExternalDataDescs[Index].Handle.DataViewIndex = (uint8)(Index + ExternalDataBaseIndex);
		}
		Handle.DataViewIndex = (uint8)(Index + ExternalDataBaseIndex);
	}
	/**
	 * Links reference to a property in instance data.
	 * Usage:
	 * 	  Linker.LinkRuntimeDataProperty(HitPointsHandle, STATETREE_INSTANCEDATA_PROPERTY(FHitPointLayout, HitPoints));
	 *
	 * @param Handle Reference to TStateTreeExternalDataHandle<> with USTRUCT type to link to.
	 * @param DummyProperty Do not use directly.
	 * @param ScriptStruct Do not use directly.
	 * @param PropertyName Do not use directly.
	 */
	template <typename T, typename S>
	void LinkInstanceDataProperty(T& Handle, const S& DummyProperty, const UScriptStruct* ScriptStruct, const TCHAR* PropertyName)
	{
		static_assert(TIsSame<typename T::DataType, S>::Value, "Expecting linked handle to have same type as the instance data struct member.");
		LinkInstanceDataPropertyInternal(Handle, ScriptStruct, PropertyName);
	}

	/** @return linked external data descriptors. */
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const { return ExternalDataDescs; }

protected:


	void LinkInstanceDataPropertyInternal(FStateTreeInstanceDataPropertyHandle& Handle, const UScriptStruct* ScriptStruct, const TCHAR* PropertyName)
	{
		check(CurrentInstanceStruct != nullptr);
		check(CurrentInstanceIndex != INDEX_NONE);

		FProperty* Property = ScriptStruct->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr)
		{
			Handle = FStateTreeInstanceDataPropertyHandle();
			Status = EStateTreeLinkerStatus::Failed;
			return;
		}

		check(CurrentInstanceIndex < MAX_uint8);
		check(Property->GetOffset_ForInternal() < MAX_uint16);
		
		Handle.DataViewIndex = (uint8)CurrentInstanceIndex;
		Handle.Type = EStateTreePropertyIndirection::Offset;
		Handle.PropertyOffset = (uint16)Property->GetOffset_ForInternal();
	}

	EStateTreeLinkerStatus Status = EStateTreeLinkerStatus::Succeeded;
	const UStruct* CurrentInstanceStruct = nullptr;
	int32 CurrentInstanceIndex = INDEX_NONE;
	int32 ExternalDataBaseIndex = 0;
	TArray<FStateTreeExternalDataDesc> ExternalDataDescs;
};
