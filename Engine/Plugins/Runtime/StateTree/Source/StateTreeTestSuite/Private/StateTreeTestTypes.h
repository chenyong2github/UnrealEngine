// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AITestsCommon.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeTestTypes.generated.h"

/**
 * This macro can be used to create property path for property binding. Checks that the member exists.
 * Example:
 *		EditorData.AddPropertyBinding(STATETREE_PROPPATH_CHECKED(EvalA, IntA), STATETREE_PROPPATH_CHECKED(TaskB1, IntB));
 **/
#define STATETREE_PROPPATH_CHECKED(Struct, MemberName) \
FStateTreeEditorPropertyPath(Struct.ID, ((void)sizeof(UEAsserts_Private::GetMemberNameCheckedJunk((&Struct)->MemberName)), TEXT(#MemberName)))



USTRUCT()
struct FTestStateTreeExecutionContext : public FStateTreeExecutionContext
{
	GENERATED_BODY()

	struct FLogItem
	{
		FLogItem() = default;
		FLogItem(const FName& InName, const FString& InMessage) : Name(InName), Message(InMessage) {}
		FName Name;
		FString Message; 
	};
	TArray<FLogItem> LogItems;
	
	void Log(const FName& Name, const FString& Message)
	{
		LogItems.Emplace(Name, Message);
	}

	void LogClear()
	{
		LogItems.Empty();
	}

	struct FLogOrder
	{
		FLogOrder(const FTestStateTreeExecutionContext& InContext, const int32 InIndex) : Context(InContext), Index(InIndex) {}

		FLogOrder Then(const FName& Name, const FString& Message) const
		{
			int32 NextIndex = Index;
			while (NextIndex < Context.LogItems.Num())
			{
				const FLogItem& Item = Context.LogItems[NextIndex];
				if (Item.Name == Name && Item.Message == Message)
				{
					break;
				}
				NextIndex++;
			}
			return FLogOrder(Context, NextIndex);
		}

		operator bool() const { return Index < Context.LogItems.Num(); }
		
		const FTestStateTreeExecutionContext& Context;
		int32 Index = 0;
	};

	FLogOrder Expect(const FName& Name, const FString& Message) const
	{
		return FLogOrder(*this, 0).Then(Name, Message);
	}
	
};


USTRUCT()
struct FTestEval_A : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	FTestEval_A() = default;
	virtual ~FTestEval_A() {}

	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	float FloatA = 0.0f;

	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	int32 IntA = 0;

	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	bool bBoolA = false;
};

USTRUCT()
struct FTestTask_B : public FStateTreeTaskBase
{
	GENERATED_BODY()

	FTestTask_B() = default;
	virtual ~FTestTask_B() {}
	
	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	float FloatB = 0.0f;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	int32 IntB = 0;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	bool bBoolB = false;
};




USTRUCT()
struct FTestMoveLocationResult : public FStateTreeResult
{
	GENERATED_BODY()

	virtual ~FTestMoveLocationResult() {}
	virtual const UScriptStruct& GetStruct() const override { return *StaticStruct(); }

	void Reset()
	{
		Location = FVector::ZeroVector;
		Direction = FVector::ZeroVector;
		Status = EStateTreeResultStatus::Unset;
	}
	
	UPROPERTY(EditAnywhere, Category = Value)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Value)
	FVector Direction = FVector::ZeroVector;

	EStateTreeResultStatus Status = EStateTreeResultStatus::Unset;
};

USTRUCT()
struct FTestPotentialSmartObjectsResult : public FStateTreeResult
{
	GENERATED_BODY()

	virtual ~FTestPotentialSmartObjectsResult() {}
	virtual const UScriptStruct& GetStruct() const override { return *StaticStruct(); }

	bool HasSmartObjects() const { return NumSmartObjects > 0 && FailedCoolDown == 0; }
	
	FName PopSmartObjectName()
	{
		FName Result;
		if (NumSmartObjects > 0)
		{
			Result = SmartObjectNames[NumSmartObjects - 1];
			NumSmartObjects--;
		}
		return Result;
	}

	void Reset()
	{
		NumSmartObjects = 0;
	}
	
	void SetFailedCoolDown()
	{
		FailedCoolDown = 4;
	}

	UPROPERTY(EditAnywhere, Category = Value)
	FName SmartObjectNames[4];

	UPROPERTY(EditAnywhere, Category = Value)
	int32 NumSmartObjects = 0;

	int32 FailedCoolDown = 0;
};

USTRUCT()
struct FTestSmartObjectResult : public FStateTreeResult
{
	GENERATED_BODY()

	virtual ~FTestSmartObjectResult() {}
	virtual const UScriptStruct& GetStruct() const override { return *StaticStruct(); }

	void Reset()
	{
		AnimationName = FName();
		SmartObjectHandle = INDEX_NONE;
		Status = EStateTreeResultStatus::Unset;
	}
	
	UPROPERTY(EditAnywhere, Category = Value)
	FName AnimationName;

	UPROPERTY(EditAnywhere, Category = Value)
	int32 SmartObjectHandle = INDEX_NONE;

	EStateTreeResultStatus Status = EStateTreeResultStatus::Unset;
};


USTRUCT()
struct FTestEval_Wander : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	FTestEval_Wander() = default;
	FTestEval_Wander(const FName InName) { Name = InName; }
	virtual ~FTestEval_Wander() {}

	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		WanderLocation = &NextMoveLocation;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));
		
		WanderLocation = &NextMoveLocation;
	}

	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Evaluate"));

		WanderLocation = &NextMoveLocation;
	}

	// True if wander location available
	UPROPERTY(EditAnywhere, Category = Eval)
	bool bHasWanderLocation = false;

	UPROPERTY(EditAnywhere, Category = Eval)
	FStateTreeResultRef WanderLocation;
	
	FTestMoveLocationResult NextMoveLocation;
};

USTRUCT()
struct FTestEval_SmartObjectSensor : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	FTestEval_SmartObjectSensor() = default;
	FTestEval_SmartObjectSensor(const FName InName) { Name = InName; }
	virtual ~FTestEval_SmartObjectSensor() {}

	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		// We expect the state of the evaluator to be valid on EnterState.
		// Evaluate() have been called during the state selection process, and some properties may have been used as conditions during select.
		
		PotentialSmartObjects = &SmartObjects;
	}
	
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		// Cleanup when we stop being used, this way the eval will be in known state when we will be ticked again later
		// during state selection.
		if (ChangeType == EStateTreeStateChangeType::Changed)
		{
			bHasSmartObjects = false;
			bIsQueryingSmartObjects = false; // Cancel a query
			SmartObjects.Reset(); // Note: this does not reset the cool down.
		}

		PotentialSmartObjects = &SmartObjects;
	}

	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Evaluate"));

		// The cool down should be a timestamp instead of a counter, to catch a case where we re-enter the evaluator after being in different state.
		if (SmartObjects.FailedCoolDown > 0)
		{
			SmartObjects.FailedCoolDown--;
		}
		bHasSmartObjects = SmartObjects.HasSmartObjects();
		
		if (bIsQueryingSmartObjects)
		{
			// Simulate Async query result
			bIsQueryingSmartObjects = false;
			bHasSmartObjects = true;
			SmartObjects.NumSmartObjects = 2;
			SmartObjects.SmartObjectNames[0] = FName();
			SmartObjects.SmartObjectNames[1] = FName(TEXT("Foo"));
		}
		if (!bHasSmartObjects && !bIsQueryingSmartObjects && SmartObjects.FailedCoolDown == 0)
		{
			// Simulate Async query start
			bIsQueryingSmartObjects = true;
		}

		// Set ref
		PotentialSmartObjects = &SmartObjects;
	}

	// Result output
	UPROPERTY(EditAnywhere, Category = Eval, meta=(Output, Struct="TestPotentialSmartObjectsResult"))
	FStateTreeResultRef PotentialSmartObjects;

	// True if any SmartObjects available
	UPROPERTY(EditAnywhere, Category = Eval)
	bool bHasSmartObjects = false;

	bool bIsQueryingSmartObjects = false;
	FTestPotentialSmartObjectsResult SmartObjects;
};


USTRUCT()
struct FTestTask_ReserveSmartObject : public FStateTreeTaskBase
{
	GENERATED_BODY()

	FTestTask_ReserveSmartObject() = default;
	FTestTask_ReserveSmartObject(const FName InName) { Name = InName; }
	virtual ~FTestTask_ReserveSmartObject() {}

	static int32 AcquireSmartObject(const FName Name)
	{
		return Name.IsNone() ? INDEX_NONE : 1;
	}

	static void ReleaseSmartObject(const int32 Handle)
	{
		// empty
	}
	
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		FTestPotentialSmartObjectsResult* PotentialSmartObjectsPtr = PotentialSmartObjects.GetMutablePtr<FTestPotentialSmartObjectsResult>();
		if (PotentialSmartObjectsPtr == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}

		// Reset outputs
		SmartObject.Reset();
		SmartObjectLocation.Reset();

		// This consumes the sensor inputs
		bool bFoundSO = false;
		while (PotentialSmartObjectsPtr->HasSmartObjects())
		{
			const FName SOName = PotentialSmartObjectsPtr->PopSmartObjectName();
			const int32 NewHandle = AcquireSmartObject(SOName);
			if (NewHandle != INDEX_NONE)
			{
				// Acquire and expose one SmartObject and the location
				SmartObject.AnimationName = SOName;
				SmartObject.SmartObjectHandle = NewHandle;
				SmartObject.Status = EStateTreeResultStatus::Unset; // Not available just yet

				SmartObjectLocation.Location = FVector(10,10,0);
				SmartObjectLocation.Direction = FVector::ForwardVector;
				SmartObjectLocation.Status = EStateTreeResultStatus::Available;

				bFoundSO = true;
				break;
			}
		}

		if (!bFoundSO)
		{
			TestContext.Log(Name, TEXT("ReserveFailed"));
			PotentialSmartObjectsPtr->SetFailedCoolDown();
			return EStateTreeRunStatus::Failed;
		}

		ReservedSmartObject = &SmartObject;
		ReservedSmartObjectLocation = &SmartObjectLocation;

		return EStateTreeRunStatus::Running;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));
		
		if (SmartObjectLocation.Status != EStateTreeResultStatus::Succeeded
			|| SmartObject.Status != EStateTreeResultStatus::Succeeded)
		{
			// Did not fully complete the SO
		}

		// Clean up
		if (SmartObject.SmartObjectHandle != INDEX_NONE)
		{
			ReleaseSmartObject(SmartObject.SmartObjectHandle);
			SmartObject.SmartObjectHandle = INDEX_NONE;
		}
		
		ReservedSmartObject = &SmartObject;
		ReservedSmartObjectLocation = &SmartObjectLocation;
	}

	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle StateCompleted) override
	{
		// Make SmartObject available only if successfully moved to it.
		if (SmartObjectLocation.Status == EStateTreeResultStatus::Succeeded)
		{
			SmartObject.Status = EStateTreeResultStatus::Available;
			SmartObjectLocation.Status = EStateTreeResultStatus::Unset;
		}

		if (SmartObjectLocation.Status == EStateTreeResultStatus::Failed
			|| SmartObject.Status == EStateTreeResultStatus::Failed)
		{
			// Failed to use the SmartObject, set cool down here to prevent SO being used in next State Select.
			// The Cooldown probably needs to be 
			FTestPotentialSmartObjectsResult* PotentialSmartObjectsPtr = PotentialSmartObjects.GetMutablePtr<FTestPotentialSmartObjectsResult>();
			if (PotentialSmartObjectsPtr)
			{
				PotentialSmartObjectsPtr->SetFailedCoolDown();
			}
		}
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		// Need to update on each update, i.e. fragment may have moved.
		ReservedSmartObject = &SmartObject;
		ReservedSmartObjectLocation = &SmartObjectLocation;

		return EStateTreeRunStatus::Running;
	}

	
	UPROPERTY(EditAnywhere, Category = Eval, meta = (Bindable, Struct="TestPotentialSmartObjectsResult"))  // In
	FStateTreeResultRef PotentialSmartObjects;

	// Because reserve is a task, these can be seen only by other Task (not evals, not conditions)
	UPROPERTY(EditAnywhere, Category = Eval, meta = (Output, Struct="TestMoveLocationResult")) // Out
	FStateTreeResultRef ReservedSmartObjectLocation;

	UPROPERTY(EditAnywhere, Category = Eval, meta = (Output, Struct="TestSmartObjectResult")) // Out
	FStateTreeResultRef ReservedSmartObject;

	FTestMoveLocationResult SmartObjectLocation;
	FTestSmartObjectResult SmartObject;
};


USTRUCT()
struct FTestTask_MoveTo : public FStateTreeTaskBase
{
	GENERATED_BODY()

	FTestTask_MoveTo() = default;
	FTestTask_MoveTo(const FName InName) { Name = InName; }
	virtual ~FTestTask_MoveTo() {}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		// Move restarts only when state changes for good, this allows to have child states running too.
		FTestMoveLocationResult* MoveLocationPtr = MoveLocation.GetMutablePtr<FTestMoveLocationResult>();
		if (MoveLocationPtr == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}
		if (MoveLocationPtr->Status != EStateTreeResultStatus::Available)
		{
			MoveLocationPtr->Status = EStateTreeResultStatus::Failed; // This probably should be a function we call, to allow prioritize the return values.
			return EStateTreeRunStatus::Failed;
		}
		MoveLocationPtr->Status = EStateTreeResultStatus::InUse;

		CurrentTick = 0;

		return EnterStateResult;
	}


	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		FTestMoveLocationResult* MoveLocationPtr = MoveLocation.GetMutablePtr<FTestMoveLocationResult>();
		if (MoveLocationPtr != nullptr)
		{
			if (MoveLocationPtr->Status == EStateTreeResultStatus::InUse)
			{
				// We got interrupted
				MoveLocationPtr->Status = EStateTreeResultStatus::Failed;
			}
		}
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		FTestMoveLocationResult* MoveLocationPtr = MoveLocation.GetMutablePtr<FTestMoveLocationResult>();
		if (MoveLocationPtr == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}

		CurrentTick++;
		const bool bDone = CurrentTick >= TicksToCompletion;
		if (bDone)
		{
			MoveLocationPtr->Status = TickResult == EStateTreeRunStatus::Succeeded ? EStateTreeResultStatus::Succeeded : EStateTreeResultStatus::Failed;
		}
		
		return bDone ? TickResult : EStateTreeRunStatus::Running;
	}

	int32 CurrentTick = 0;

	UPROPERTY(EditAnywhere, Category = Eval, meta = (Bindable))
	FStateTreeResultRef MoveLocation;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	int32 TicksToCompletion = 2;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	EStateTreeRunStatus TickResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};

USTRUCT()
struct FTestTask_Stand : public FStateTreeTaskBase
{
	GENERATED_BODY()

	FTestTask_Stand() = default;
	FTestTask_Stand(const FName InName) { Name = InName; }
	virtual ~FTestTask_Stand() {}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));
		
		if (ChangeType == EStateTreeStateChangeType::Changed)
		{
			CurrentTick = 0;
		}
		return EnterStateResult;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));
		
		CurrentTick++;
		return (CurrentTick >= TicksToCompletion) ? TickResult : EStateTreeRunStatus::Running;
	};

	int32 CurrentTick = 0;
	
	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	int32 TicksToCompletion = 1;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	EStateTreeRunStatus TickResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};

USTRUCT()
struct FTestTask_UseSmartObject : public FStateTreeTaskBase
{
	GENERATED_BODY()

	FTestTask_UseSmartObject() = default;
	FTestTask_UseSmartObject(const FName InName) { Name = InName; }
	virtual ~FTestTask_UseSmartObject() {}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));
		
		FTestSmartObjectResult* SmartObjectPtr = SmartObject.GetMutablePtr<FTestSmartObjectResult>();
		if (SmartObjectPtr == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}
		if (SmartObjectPtr->Status != EStateTreeResultStatus::Available)
		{
			SmartObjectPtr->Status = EStateTreeResultStatus::Failed;
			return EStateTreeRunStatus::Failed;
		}
		CurrentTick = 0;
		SmartObjectPtr->Status = EStateTreeResultStatus::InUse;

		return EnterStateResult;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		FTestSmartObjectResult* SmartObjectPtr = SmartObject.GetMutablePtr<FTestSmartObjectResult>();
		if (SmartObjectPtr != nullptr)
		{
			if (SmartObjectPtr->Status != EStateTreeResultStatus::Succeeded)
			{
				// We got interrupted
				SmartObjectPtr->Status = EStateTreeResultStatus::Failed;
			}
		}
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		FTestSmartObjectResult* SmartObjectPtr = SmartObject.GetMutablePtr<FTestSmartObjectResult>();
		if (SmartObjectPtr == nullptr)
		{
			return EStateTreeRunStatus::Failed;
		}
		
		CurrentTick++;
		
		const bool bDone = CurrentTick >= TicksToCompletion;
		if (bDone)
		{
			SmartObjectPtr->Status = TickResult == EStateTreeRunStatus::Succeeded ? EStateTreeResultStatus::Succeeded : EStateTreeResultStatus::Failed;
		}
		
		return bDone ? TickResult : EStateTreeRunStatus::Running;
	}

	int32 CurrentTick = 0;

	UPROPERTY(EditAnywhere, Category = Eval, meta = (Bindable, Struct="TestSmartObjectResult")) // In
	FStateTreeResultRef SmartObject;

	UPROPERTY(EditAnywhere, Category = Task)
	int32 TicksToCompletion = 2;

	UPROPERTY(EditAnywhere, Category = Task)
	EStateTreeRunStatus TickResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Task)
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};