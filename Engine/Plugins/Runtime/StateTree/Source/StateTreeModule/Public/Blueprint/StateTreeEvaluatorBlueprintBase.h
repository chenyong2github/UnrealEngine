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

	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "EnterState", DeprecatedFunction, DeprecationMessage = "Evaluators are not ticked at state level anymore, Use ReceiveTreeStart instead."))
	void ReceiveEnterState(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ExitState", DeprecatedFunction, DeprecationMessage = "Evaluators are not ticked at state level anymore, Use ReceiveTreeStop."))
	void ReceiveExitState(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "StateCompleted", DeprecatedFunction, DeprecationMessage = "Evaluators are not ticked at state level anymore, Use ReceiveTreeStop instead."))
	void ReceiveStateCompleted(AActor* OwnerActor, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates CompletedState);

	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Evaluate", DeprecatedFunction, DeprecationMessage = "Use ReceiveTick instead."))
	void ReceiveEvaluate(AActor* OwnerActor, const EStateTreeEvaluationType EvalType, const float DeltaTime);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "TreeStart"))
	void ReceiveTreeStart(AActor* OwnerActor);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "TreeStop"))
	void ReceiveTreeStop(AActor* OwnerActor);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Tick"))
	void ReceiveTick(AActor* OwnerActor, const float DeltaTime);
	
	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);
	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);
	UE_DEPRECATED(5.1, "This function will be removed for 5.1.")
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates& CompletedActiveStates);

	virtual void TreeStart(FStateTreeExecutionContext& Context);
	virtual void TreeStop(FStateTreeExecutionContext& Context);
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime);
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
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeActiveStates& CompletedActiveStates) const override;

	virtual void TreeStart(FStateTreeExecutionContext& Context) const override;
	virtual void TreeStop(FStateTreeExecutionContext& Context) const override;
	virtual void Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	UPROPERTY()
	TSubclassOf<UStateTreeEvaluatorBlueprintBase> EvaluatorClass = nullptr;

	TArray<FStateTreeBlueprintExternalDataHandle> ExternalDataHandles;
};
