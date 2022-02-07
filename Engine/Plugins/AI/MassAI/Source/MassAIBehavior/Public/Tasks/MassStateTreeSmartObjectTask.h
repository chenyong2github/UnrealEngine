// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassSmartObjectRequest.h"
#include "MassStateTreeTypes.h"
#include "MassSmartObjectTypes.h"

#include "MassStateTreeSmartObjectTask.generated.h"

struct FStateTreeExecutionContext;
struct FMassSmartObjectUserFragment;
class USmartObjectSubsystem;
class UMassSignalSubsystem;
struct FTransformFragment;
struct FMassMoveTargetFragment;

/**
 * Tasks to claim a smart object from search results and release it when done.
 */
USTRUCT()
struct MASSAIBEHAVIOR_API FMassClaimSmartObjectTaskInstanceData
{
	GENERATED_BODY()

	/** Result of the candidates search request (Input) */
	UPROPERTY(VisibleAnywhere, Category = Input)
	FMassSmartObjectRequestResult SearchRequestResult;

	/** Result of the claim on potential candidates from the search results (Output) */
	UPROPERTY(VisibleAnywhere, Category = Output)
	EMassSmartObjectClaimResult ClaimResult = EMassSmartObjectClaimResult::Unset;
};

USTRUCT(meta = (DisplayName = "Claim SmartObject"))
struct MASSAIBEHAVIOR_API FMassClaimSmartObjectTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FMassClaimSmartObjectTaskInstanceData::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;

	TStateTreeExternalDataHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
	TStateTreeExternalDataHandle<UMassSignalSubsystem> MassSignalSubsystemHandle;

	TStateTreeInstanceDataPropertyHandle<FMassSmartObjectRequestResult> SearchRequestResultHandle;
	TStateTreeInstanceDataPropertyHandle<EMassSmartObjectClaimResult> ClaimResultHandle;
};

/**
 * Task to tell an entity to start using a claimed smart object.
 */
USTRUCT()
struct MASSAIBEHAVIOR_API FMassUseSmartObjectTaskInstanceData
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Mass Use SmartObject Task"))
struct MASSAIBEHAVIOR_API FMassUseSmartObjectTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual const UStruct* GetInstanceDataType() const override { return FMassUseSmartObjectTaskInstanceData::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;

	TStateTreeExternalDataHandle<USmartObjectSubsystem> SmartObjectSubsystemHandle;
	TStateTreeExternalDataHandle<UMassSignalSubsystem> MassSignalSubsystemHandle;
	TStateTreeExternalDataHandle<FTransformFragment> EntityTransformHandle;
	TStateTreeExternalDataHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeExternalDataHandle<FMassMoveTargetFragment> MoveTargetHandle;

	/** Delay in seconds before trying to find & use another smart object */
	UPROPERTY(EditAnywhere, Category = Parameter)
	float Cooldown = 0.f;
};

