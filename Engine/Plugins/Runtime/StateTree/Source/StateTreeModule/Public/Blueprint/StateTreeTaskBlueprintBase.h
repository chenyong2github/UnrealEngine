// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "StateTreeTypes.h"
#include "StateTreeTaskBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeItemBlueprintBase.h"
#include "StateTreeTaskBlueprintBase.generated.h"

struct FStateTreeExecutionContext;

/*
 * Base class for Blueprint based Tasks. 
 */
UCLASS(Abstract, Blueprintable)
class STATETREEMODULE_API UStateTreeTaskBlueprintBase : public UStateTreeItemBlueprintBase
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintNativeEvent, meta = (DisplayName = "EnterState"))
	EStateTreeRunStatus ReceiveEnterState(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ExitState"))
	void ReceiveExitState(AActor* OwnerActor, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "StateCompleted"))
	void ReceiveStateCompleted(AActor* OwnerActor, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState);

	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Tick"))
	EStateTreeRunStatus ReceiveTick(AActor* OwnerActor, const float DeltaTime);

	
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition);

	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState);

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime);
};

/**
 * Wrapper for Blueprint based Tasks.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeBlueprintTaskWrapper : public FStateTreeTaskBase
{
	GENERATED_BODY()

	virtual const UStruct* GetInstanceDataType() const override { return TaskClass; };
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	UPROPERTY()
	TSubclassOf<UStateTreeTaskBlueprintBase> TaskClass = nullptr;

	TArray<FStateTreeBlueprintExternalDataHandle> ExternalDataHandles;
};
