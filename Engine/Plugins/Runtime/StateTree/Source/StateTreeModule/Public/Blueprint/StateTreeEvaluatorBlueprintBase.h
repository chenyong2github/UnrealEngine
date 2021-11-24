// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "StateTreeTypes.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeItemBlueprintBase.h"
#include "StateTreeEvaluatorBlueprintBase.generated.h"

struct FStateTreeExecutionContext;

/*
 * Base class for Blueprint based evaluators. 
 */
UCLASS(Abstract, Blueprintable)
class STATETREEMODULE_API UStateTreeEvaluatorBlueprintBase : public UStateTreeItemBlueprintBase
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "EnterState"))
	void ReceiveEnterState(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ExitState"))
	void ReceiveExitState(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "StateCompleted"))
	void ReceiveStateCompleted(AActor* OwnerActor, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Evaluate"))
	void ReceiveEvaluate(AActor* OwnerActor, const EStateTreeEvaluationType EvalType, const float DeltaTime);

	
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState);

	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime);
};

/**
 * Wrapper for Blueprint based Evaluators.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeBlueprintEvaluatorWrapper : public FStateTreeEvaluatorBase
{
	GENERATED_BODY()

	virtual const UStruct* GetInstanceDataType() const override { return EvaluatorClass; };
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const override;

	UPROPERTY()
	TSubclassOf<UStateTreeEvaluatorBlueprintBase> EvaluatorClass = nullptr;

	TArray<FStateTreeBlueprintExternalDataHandle> ExternalDataHandles;
};
