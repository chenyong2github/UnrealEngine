// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AITestsCommon.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeTestTypes.generated.h"


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
struct FTestEval_AInstanceData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	float FloatA = 0.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 IntA = 0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bBoolA = false;
};

USTRUCT()
struct FTestEval_A : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	typedef FTestEval_AInstanceData InstanceDataType;

	FTestEval_A() = default;
	virtual ~FTestEval_A() override {}

	virtual const UStruct* GetInstanceDataType() const override { return FTestEval_AInstanceData::StaticStruct(); }
	
	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(FloatAHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_AInstanceData, FloatA));
		Linker.LinkInstanceDataProperty(IntAHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_AInstanceData, IntA));
		Linker.LinkInstanceDataProperty(BoolAHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_AInstanceData, bBoolA));
		return true;
	}

	TStateTreeInstanceDataPropertyHandle<float> FloatAHandle;
	TStateTreeInstanceDataPropertyHandle<int32> IntAHandle;
	TStateTreeInstanceDataPropertyHandle<bool> BoolAHandle;
};

USTRUCT()
struct FTestTask_BInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	float FloatB = 0.0f;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 IntB = 0;

	UPROPERTY(EditAnywhere, Category = Parameter)
	bool bBoolB = false;
};

USTRUCT()
struct FTestTask_B : public FStateTreeTaskBase
{
	GENERATED_BODY()

	typedef FTestTask_BInstanceData InstanceDataType;

	FTestTask_B() = default;
	virtual ~FTestTask_B() override {}
	
	virtual const UStruct* GetInstanceDataType() const override { return FTestTask_BInstanceData::StaticStruct(); }
	
	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(FloatBHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_BInstanceData, FloatB));
		Linker.LinkInstanceDataProperty(IntBHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_BInstanceData, IntB));
		Linker.LinkInstanceDataProperty(BoolBHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_BInstanceData, bBoolB));
		return true;
	}

	TStateTreeInstanceDataPropertyHandle<float> FloatBHandle;
	TStateTreeInstanceDataPropertyHandle<int32> IntBHandle;
	TStateTreeInstanceDataPropertyHandle<bool> BoolBHandle;
};

USTRUCT()
struct FTestMoveLocationResult
{
	GENERATED_BODY()

	void Reset()
	{
		Location = FVector::ZeroVector;
		Direction = FVector::ZeroVector;
	}
	
	UPROPERTY(EditAnywhere, Category = Value)
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Value)
	FVector Direction = FVector::ZeroVector;
};

USTRUCT()
struct FTestPotentialSmartObjectsResult
{
	GENERATED_BODY()

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
struct FTestSmartObjectResult
{
	GENERATED_BODY()

	bool IsValid() const { return SmartObjectHandle != INDEX_NONE; }
	
	void Reset()
	{
		AnimationName = FName();
		SmartObjectHandle = INDEX_NONE;
	}
	
	UPROPERTY(EditAnywhere, Category = Value)
	FName AnimationName;

	UPROPERTY(EditAnywhere, Category = Value)
	int32 SmartObjectHandle = INDEX_NONE;
};

USTRUCT()
struct FTestEval_WanderInstanceData
{
	GENERATED_BODY()

	// True if wander location available
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasWanderLocation = false;

	UPROPERTY(EditAnywhere, Category = Output)
	FTestMoveLocationResult NextMoveLocation;
};

USTRUCT()
struct FTestEval_Wander : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	typedef FTestEval_WanderInstanceData InstanceDataType;
	
	FTestEval_Wander() = default;
	FTestEval_Wander(const FName InName) { Name = InName; }
	virtual ~FTestEval_Wander() override {}

	virtual const UStruct* GetInstanceDataType() const override { return FTestEval_WanderInstanceData::StaticStruct(); }
	
	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(HasWanderLocationHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_WanderInstanceData, bHasWanderLocation));
		Linker.LinkInstanceDataProperty(NextMoveLocationHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_WanderInstanceData, NextMoveLocation));
		return true;
	}

	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		bool& bHasWanderLocation = Context.GetInstanceData(HasWanderLocationHandle);
		FTestMoveLocationResult& NextMoveLocation = Context.GetInstanceData(NextMoveLocationHandle);

		bHasWanderLocation = true;
		NextMoveLocation.Location = FVector::ZeroVector;
		NextMoveLocation.Direction = FVector::ForwardVector;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));
	}

	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Evaluate"));
	}
	
	TStateTreeInstanceDataPropertyHandle<bool> HasWanderLocationHandle;
	TStateTreeInstanceDataPropertyHandle<FTestMoveLocationResult> NextMoveLocationHandle;
};

USTRUCT()
struct FTestEval_SmartObjectSensorInstanceData
{
	GENERATED_BODY()

	// Result output
	UPROPERTY(EditAnywhere, Category = Output)
	FTestPotentialSmartObjectsResult PotentialSmartObjects;

	// True if any SmartObjects available
	UPROPERTY(EditAnywhere, Category = Output)
	bool bHasSmartObjects = false;

	UPROPERTY()
	bool bIsQueryingSmartObjects = false;
};

USTRUCT()
struct FTestEval_SmartObjectSensor : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	typedef FTestEval_SmartObjectSensorInstanceData InstanceDataType;

	FTestEval_SmartObjectSensor() = default;
	FTestEval_SmartObjectSensor(const FName InName) { Name = InName; }
	virtual ~FTestEval_SmartObjectSensor() {}

	virtual const UStruct* GetInstanceDataType() const override { return FTestEval_SmartObjectSensorInstanceData::StaticStruct(); }
	
	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(PotentialSmartObjectsHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_SmartObjectSensorInstanceData, PotentialSmartObjects));
		Linker.LinkInstanceDataProperty(HasSmartObjectsHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_SmartObjectSensorInstanceData, bHasSmartObjects));
		Linker.LinkInstanceDataProperty(IsQueryingSmartObjectsHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestEval_SmartObjectSensorInstanceData, bIsQueryingSmartObjects));
		return true;
	}

	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		// We expect the state of the evaluator to be valid on EnterState.
		// Evaluate() have been called during the state selection process, and some properties may have been used as conditions during select.
	}
	
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		// Cleanup when we stop being used, this way the eval will be in known state when we will be ticked again later
		// during state selection.
		if (ChangeType == EStateTreeStateChangeType::Changed)
		{
			FTestPotentialSmartObjectsResult& PotentialSmartObjects = Context.GetInstanceData(PotentialSmartObjectsHandle);
			bool& bHasSmartObjects = Context.GetInstanceData(HasSmartObjectsHandle);
			bool& bIsQueryingSmartObjects = Context.GetInstanceData(IsQueryingSmartObjectsHandle);

			bHasSmartObjects = false;
			bIsQueryingSmartObjects = false; // Cancel a query
			PotentialSmartObjects.Reset(); // Note: this does not reset the cool down.
		}
	}

	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Evaluate"));

		FTestPotentialSmartObjectsResult& PotentialSmartObjects = Context.GetInstanceData(PotentialSmartObjectsHandle);
		bool& bHasSmartObjects = Context.GetInstanceData(HasSmartObjectsHandle);
		bool& bIsQueryingSmartObjects = Context.GetInstanceData(IsQueryingSmartObjectsHandle);

		// The cool down should be a timestamp instead of a counter, to catch a case where we re-enter the evaluator after being in different state.
		if (PotentialSmartObjects.FailedCoolDown > 0)
		{
			PotentialSmartObjects.FailedCoolDown--;
		}
		bHasSmartObjects = PotentialSmartObjects.HasSmartObjects();
		
		if (bIsQueryingSmartObjects)
		{
			// Simulate Async query result
			bIsQueryingSmartObjects = false;
			bHasSmartObjects = true;
			PotentialSmartObjects.NumSmartObjects = 2;
			PotentialSmartObjects.SmartObjectNames[0] = FName();
			PotentialSmartObjects.SmartObjectNames[1] = FName(TEXT("Foo"));
		}
		if (!bHasSmartObjects && !bIsQueryingSmartObjects && PotentialSmartObjects.FailedCoolDown == 0)
		{
			// Simulate Async query start
			bIsQueryingSmartObjects = true;
		}
	}

	TStateTreeInstanceDataPropertyHandle<FTestPotentialSmartObjectsResult> PotentialSmartObjectsHandle;
	TStateTreeInstanceDataPropertyHandle<bool> HasSmartObjectsHandle;
	TStateTreeInstanceDataPropertyHandle<bool> IsQueryingSmartObjectsHandle;

};


USTRUCT()
struct FTestTask_ReserveSmartObjectInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FTestPotentialSmartObjectsResult PotentialSmartObjects;

	// Because reserve is a task, these can be seen only by other Task (not evals, not conditions)
	UPROPERTY(EditAnywhere, Category = Output)
	FTestMoveLocationResult ReservedSmartObjectLocation;

	UPROPERTY(EditAnywhere, Category = Output)
	FTestSmartObjectResult ReservedSmartObject;
};

USTRUCT()
struct FTestTask_ReserveSmartObject : public FStateTreeTaskBase
{
	GENERATED_BODY()

	typedef FTestTask_ReserveSmartObjectInstanceData InstanceDataType;

	FTestTask_ReserveSmartObject() = default;
	FTestTask_ReserveSmartObject(const FName InName) { Name = InName; }
	virtual ~FTestTask_ReserveSmartObject() override {}

	virtual const UStruct* GetInstanceDataType() const override { return FTestTask_ReserveSmartObjectInstanceData::StaticStruct(); }
	
	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(PotentialSmartObjectsHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_ReserveSmartObjectInstanceData, PotentialSmartObjects));
		Linker.LinkInstanceDataProperty(ReservedSmartObjectLocationHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_ReserveSmartObjectInstanceData, ReservedSmartObjectLocation));
		Linker.LinkInstanceDataProperty(ReservedSmartObjectHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_ReserveSmartObjectInstanceData, ReservedSmartObject));
		return true;
	}

	static int32 AcquireSmartObject(const FName Name)
	{
		return Name.IsNone() ? INDEX_NONE : 1;
	}

	static void ReleaseSmartObject(const int32 Handle)
	{
		// empty
	}
	
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		const FTestPotentialSmartObjectsResult& PotentialSmartObjects = Context.GetInstanceData(PotentialSmartObjectsHandle);
		FTestMoveLocationResult& ReservedSmartObjectLocation = Context.GetInstanceData(ReservedSmartObjectLocationHandle);
		FTestSmartObjectResult& ReservedSmartObject = Context.GetInstanceData(ReservedSmartObjectHandle);

		// Reset outputs
		ReservedSmartObject.Reset();
		ReservedSmartObjectLocation.Reset();

		// This consumes the sensor inputs
		bool bFoundSO = false;
		while (PotentialSmartObjects.HasSmartObjects())
		{
			const FName SOName = FName(); // PotentialSmartObjects.PopSmartObjectName(); // @todo: This is currently not allowed, all inputs are const
			const int32 NewHandle = AcquireSmartObject(SOName);
			if (NewHandle != INDEX_NONE)
			{
				// Acquire and expose one SmartObject and the location
				ReservedSmartObject.AnimationName = SOName;
				ReservedSmartObject.SmartObjectHandle = NewHandle;

				ReservedSmartObjectLocation.Location = FVector(10,10,0);
				ReservedSmartObjectLocation.Direction = FVector::ForwardVector;

				bFoundSO = true;
				break;
			}
		}

		if (!bFoundSO)
		{
			TestContext.Log(Name, TEXT("ReserveFailed"));
			// PotentialSmartObjects.SetFailedCoolDown(); // @todo: This is currently not allowed, all inputs are const
			return EStateTreeRunStatus::Failed;
		}

		return EStateTreeRunStatus::Running;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		FTestSmartObjectResult& ReservedSmartObject = Context.GetInstanceData(ReservedSmartObjectHandle);

		// @todo: redo this logic
/*		if (SmartObjectLocation.Status != EStateTreeResultStatus::Succeeded
			|| SmartObject.Status != EStateTreeResultStatus::Succeeded)
		{
			// Did not fully complete the SO
		}*/

		// Clean up
		if (ReservedSmartObject.SmartObjectHandle != INDEX_NONE)
		{
			ReleaseSmartObject(ReservedSmartObject.SmartObjectHandle);
			ReservedSmartObject.SmartObjectHandle = INDEX_NONE;
		}
	}

	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle StateCompleted) const override
	{
		const FTestPotentialSmartObjectsResult& PotentialSmartObjects = Context.GetInstanceData(PotentialSmartObjectsHandle);
		FTestMoveLocationResult& ReservedSmartObjectLocation = Context.GetInstanceData(ReservedSmartObjectLocationHandle);
		FTestSmartObjectResult& ReservedSmartObject = Context.GetInstanceData(ReservedSmartObjectHandle);

		// @todo: redo this logic
/*		// Make SmartObject available only if successfully moved to it.
		if (ReservedSmartObjectLocation.Status == EStateTreeResultStatus::Succeeded)
		{
			ReservedSmartObject.Status = EStateTreeResultStatus::Available;
			ReservedSmartObjectLocation.Status = EStateTreeResultStatus::Unset;
		}

		if (ReservedSmartObjectLocation.Status == EStateTreeResultStatus::Failed
			|| ReservedSmartObject.Status == EStateTreeResultStatus::Failed)
		{
			// Failed to use the SmartObject, set cool down here to prevent SO being used in next State Select.
			// The Cooldown probably needs to be 
			PotentialSmartObjects.SetFailedCoolDown();
		}*/
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		return EStateTreeRunStatus::Running;
	}

	TStateTreeInstanceDataPropertyHandle<FTestPotentialSmartObjectsResult> PotentialSmartObjectsHandle;
	TStateTreeInstanceDataPropertyHandle<FTestMoveLocationResult> ReservedSmartObjectLocationHandle;
	TStateTreeInstanceDataPropertyHandle<FTestSmartObjectResult> ReservedSmartObjectHandle;
};


USTRUCT()
struct FTestTask_MoveToInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Input)
	FTestMoveLocationResult MoveLocation;

	UPROPERTY()
	int32 CurrentTick = 0;
};

USTRUCT()
struct FTestTask_MoveTo : public FStateTreeTaskBase
{
	GENERATED_BODY()

	typedef FTestTask_MoveToInstanceData InstanceDataType;

	FTestTask_MoveTo() = default;
	FTestTask_MoveTo(const FName InName) { Name = InName; }
	virtual ~FTestTask_MoveTo() {}

	virtual const UStruct* GetInstanceDataType() const override { return FTestTask_MoveToInstanceData::StaticStruct(); }
	
	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(MoveLocationHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_MoveToInstanceData, MoveLocation));
		Linker.LinkInstanceDataProperty(CurrentTickHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_MoveToInstanceData, CurrentTick));
		return true;
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		FTestMoveLocationResult& MoveLocation = Context.GetInstanceData(MoveLocationHandle);
		int32& CurrentTick = Context.GetInstanceData(CurrentTickHandle);

		// @todo: This is currently not allowed, all inputs are const
//		MoveLocation.Status = EStateTreeResultStatus::InUse;

		CurrentTick = 0;

		return EnterStateResult;
	}


	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		FTestMoveLocationResult& MoveLocation = Context.GetInstanceData(MoveLocationHandle);

		// @todo: This is currently not allowed, all inputs are const
/*		if (MoveLocation.Status == EStateTreeResultStatus::InUse)
		{
			// We got interrupted
			MoveLocation.Status = EStateTreeResultStatus::Failed;
		}*/
	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		FTestMoveLocationResult& MoveLocation = Context.GetInstanceData(MoveLocationHandle);
		int32& CurrentTick = Context.GetInstanceData(CurrentTickHandle);

		CurrentTick++;
		const bool bDone = CurrentTick >= TicksToCompletion;
		if (bDone)
		{
			// @todo: This is currently not allowed, all inputs are const
//			MoveLocation.Status = TickResult == EStateTreeRunStatus::Succeeded ? EStateTreeResultStatus::Succeeded : EStateTreeResultStatus::Failed;
		}
		
		return bDone ? TickResult : EStateTreeRunStatus::Running;
	}

	TStateTreeInstanceDataPropertyHandle<FTestMoveLocationResult> MoveLocationHandle;
	TStateTreeInstanceDataPropertyHandle<int32> CurrentTickHandle;
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 TicksToCompletion = 2;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus TickResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};

USTRUCT()
struct FTestTask_StandInstanceData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 CurrentTick = 0;
};

USTRUCT()
struct FTestTask_Stand : public FStateTreeTaskBase
{
	GENERATED_BODY()

	typedef FTestTask_StandInstanceData InstanceDataType;
	
	FTestTask_Stand() = default;
	FTestTask_Stand(const FName InName) { Name = InName; }
	virtual ~FTestTask_Stand() {}

	virtual const UStruct* GetInstanceDataType() const { return FTestTask_StandInstanceData::StaticStruct(); }

	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(CurrentTickHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_StandInstanceData, CurrentTick));
		return true;
	}
	
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		int32& CurrentTick = Context.GetInstanceData(CurrentTickHandle);
		
		if (ChangeType == EStateTreeStateChangeType::Changed)
		{
			CurrentTick = 0;
		}
		return EnterStateResult;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));
	}

	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("StateCompleted"));
	}
	
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		int32& CurrentTick = Context.GetInstanceData(CurrentTickHandle);
		CurrentTick++;
		
		return (CurrentTick >= TicksToCompletion) ? TickResult : EStateTreeRunStatus::Running;
	};

	TStateTreeInstanceDataPropertyHandle<int32> CurrentTickHandle;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 TicksToCompletion = 1;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus TickResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};


USTRUCT()
struct FTestTask_UseSmartObjectInstanceData
{
	GENERATED_BODY()

	UPROPERTY()
	int32 CurrentTick = 0;

	UPROPERTY(EditAnywhere, Category = Input) // In
	FTestSmartObjectResult SmartObject;
};


USTRUCT()
struct FTestTask_UseSmartObject : public FStateTreeTaskBase
{
	GENERATED_BODY()

	typedef FTestEval_WanderInstanceData InstanceDataType;
	
	FTestTask_UseSmartObject() = default;
	FTestTask_UseSmartObject(const FName InName) { Name = InName; }
	virtual ~FTestTask_UseSmartObject() {}

	virtual const UStruct* GetInstanceDataType()const override { return FTestTask_UseSmartObjectInstanceData::StaticStruct(); }

	virtual bool Link(FStateTreeLinker& Linker) override
	{
		Linker.LinkInstanceDataProperty(CurrentTickHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_UseSmartObjectInstanceData, CurrentTick));
		Linker.LinkInstanceDataProperty(SmartObjectHandle, STATETREE_INSTANCEDATA_PROPERTY(FTestTask_UseSmartObjectInstanceData, SmartObject));

		return true;
	}

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("EnterState"));

		FTestSmartObjectResult& SmartObject = Context.GetInstanceData(SmartObjectHandle);
		int32& CurrentTick = Context.GetInstanceData(CurrentTickHandle);
		
		// @todo: This is currently not allowed, all inputs are const
/*		if (SmartObjectPtr.Status != EStateTreeResultStatus::Available)
		{
			SmartObjectPtr.Status = EStateTreeResultStatus::Failed;
			return EStateTreeRunStatus::Failed;
		}*/
		
		CurrentTick = 0;
//		SmartObjectPtr.Status = EStateTreeResultStatus::InUse;

		return EnterStateResult;
	}

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("ExitState"));

		FTestSmartObjectResult& SmartObject = Context.GetInstanceData(SmartObjectHandle);

		// @todo: This is currently not allowed, all inputs are const
/*		if (SmartObject.Status != EStateTreeResultStatus::Succeeded)
		{
			// We got interrupted
			SmartObject.Status = EStateTreeResultStatus::Failed;
		}*/

	}

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override
	{
		FTestStateTreeExecutionContext& TestContext = static_cast<FTestStateTreeExecutionContext&>(Context);
		TestContext.Log(Name, TEXT("Tick"));

		FTestSmartObjectResult& SmartObject = Context.GetInstanceData(SmartObjectHandle);
		int32& CurrentTick = Context.GetInstanceData(CurrentTickHandle);
		
		CurrentTick++;
		
		const bool bDone = CurrentTick >= TicksToCompletion;
		if (bDone)
		{
			// @todo: This is currently not allowed, all inputs are const
//			SmartObject.Status = TickResult == EStateTreeRunStatus::Succeeded ? EStateTreeResultStatus::Succeeded : EStateTreeResultStatus::Failed;
		}
		
		return bDone ? TickResult : EStateTreeRunStatus::Running;
	}

	TStateTreeInstanceDataPropertyHandle<int32> CurrentTickHandle;
	TStateTreeInstanceDataPropertyHandle<FTestSmartObjectResult> SmartObjectHandle;

	UPROPERTY(EditAnywhere, Category = Parameter)
	int32 TicksToCompletion = 2;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus TickResult = EStateTreeRunStatus::Succeeded;

	UPROPERTY(EditAnywhere, Category = Parameter)
	EStateTreeRunStatus EnterStateResult = EStateTreeRunStatus::Running;
};

