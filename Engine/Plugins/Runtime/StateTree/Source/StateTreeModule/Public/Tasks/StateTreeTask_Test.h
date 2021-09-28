// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTask_Test.generated.h"


UCLASS(BlueprintType, EditInlineNew, CollapseCategories)
class STATETREEMODULE_API UStateTreeTask_Test : public UStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	UStateTreeTask_Test(const FObjectInitializer& ObjectInitializer);

	/** Called when instantiated first time. */
	virtual bool Initialize(FStateTreeInstance& StateTreeInstance) override;
	/** Called when evaluator becomes active/ticking. */
	virtual void Activate(FStateTreeInstance& StateTreeInstance)  override;
	/** Called when evaluator becomes inactive. */
	virtual void Deactivate(FStateTreeInstance& StateTreeInstance)  override;

	/** Called on each tick. */
	virtual EStateTreeRunStatus Tick(FStateTreeInstance& StateTreeInstance, const float DeltaTime) override;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeInstance& StateTreeInstance) const;
#endif

#if WITH_EDITOR
	/** Create Task instance template, set default variables, and resolve variable handles. */
	virtual bool ResolveVariables(const FStateTreeVariableLayout& Variables, FStateTreeConstantStorage& Constants, UObject* Outer) override;
#endif

	float Time = 0.0f;

	UPROPERTY(EditAnywhere, Category = Task)
	float Duration = 0.0f;

	UPROPERTY(EditAnywhere, Category = Task)
	bool bFail = false;
};

USTRUCT(meta = (DisplayName = "Task Test2"))
struct STATETREEMODULE_API FStateTreeTask2_Test : public FStateTreeTask2Base
{
	GENERATED_BODY()

	FStateTreeTask2_Test();
	virtual ~FStateTreeTask2_Test();
	
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

#if WITH_GAMEPLAY_DEBUGGER
	virtual void AppendDebugInfoString(FString& DebugString, const FStateTreeExecutionContext& Context) const;
#endif

protected:
	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	float Value = 0.0f;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	FVector VectorValue = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	int32 IntVal = 0;

	UPROPERTY(EditAnywhere, Category = Task)
	int32 IntVal2 = 0;

	UPROPERTY(EditAnywhere, Category = Task, meta = (Bindable))
	bool bBoolVal = false;
};
