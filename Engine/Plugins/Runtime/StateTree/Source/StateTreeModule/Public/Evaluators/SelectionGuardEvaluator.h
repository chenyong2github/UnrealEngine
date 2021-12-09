// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeEvaluatorBase.h"
#include "SelectionGuardEvaluator.generated.h"

struct FStateTreeExecutionContext;

/**
 * General purpose evaluator for guarding against selecting an active state.
 * - Add the guard evaluator to the state (and sub-state) that you want to prevent re-selecting
 * - Use the bActive in a condition to guard entering the state.
 */

UENUM()
enum class ESelectionGuardActivation : uint8
{
	OnEnterState,	// Activate guard when entering the state.
	OnCompleted,	// Activate guard when state is completed (either success or failure).
	OnFailed,		// Activate guard when state failed.
	OnSucceeded,	// Activate guard when state succeeded.
};

USTRUCT()
struct STATETREEMODULE_API FSelectionGuardEvaluatorInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Output", Meta=(Tooltip = "Cooldown is active."))
	bool bActive = false;
};

USTRUCT(DisplayName="Selection Guard")
struct STATETREEMODULE_API FSelectionGuardEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	FSelectionGuardEvaluator() = default;
	virtual ~FSelectionGuardEvaluator() override {}

	virtual const UStruct* GetInstanceDataType() const override { return FSelectionGuardEvaluatorInstanceData::StaticStruct(); };
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	
	TStateTreeInstanceDataPropertyHandle<bool> ActiveHandle;

	UPROPERTY(EditAnywhere, Category="Default", Meta=(Tooltip = "Describes when to turn on the guard."))
	ESelectionGuardActivation Activation = ESelectionGuardActivation::OnEnterState;
};
