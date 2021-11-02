// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSmartObjectRequest.h"
#include "MassStateTreeTypes.h"
#include "MassSmartObjectTypes.h"

#include "MassStateTreeSmartObjectTask.generated.h"

struct FStateTreeExecutionContext;
struct FMassSmartObjectUserFragment;
class USmartObjectSubsystem;
struct FDataFragment_Transform;
struct FMassMoveTargetFragment;

/**
 * Tasks to claim a smart object from search results and release it when done.
 */
USTRUCT(meta = (DisplayName = "Claim SmartObject"))
struct MASSAIBEHAVIOR_API FMassClaimSmartObjectTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	TStateTreeItemHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeItemHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;

	/** Result of the candidates search request (Input) */
	UPROPERTY(VisibleAnywhere, Category = SmartObject, meta = (Bindable), Transient)
	FMassSmartObjectRequestResult SearchRequestResult;

	/** Result of the claim on potential candidates from the search results (Output) */
	UPROPERTY(VisibleAnywhere, Category = SmartObject, Transient)
	EMassSmartObjectClaimResult ClaimResult = EMassSmartObjectClaimResult::Unset;
};

/**
 * Task to tell an entity to start using a claimed smart object.
 */
USTRUCT(meta = (DisplayName = "Mass Use SmartObject Task"))
struct MASSAIBEHAVIOR_API FMassUseSmartObjectTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	TStateTreeItemHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
	TStateTreeItemHandle<FDataFragment_Transform> EntityTransformHandle;
	TStateTreeItemHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeItemHandle<FMassMoveTargetFragment> MoveTargetHandle;

	/** Delay in seconds before trying to find & use another smart object */
	UPROPERTY(EditAnywhere, Category = SmartObject)
	float Cooldown = 0.f;
};

