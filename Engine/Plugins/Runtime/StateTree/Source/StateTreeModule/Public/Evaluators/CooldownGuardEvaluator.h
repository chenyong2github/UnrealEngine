// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeEvaluatorBase.h"
#include "CooldownGuardEvaluator.generated.h"

struct FStateTreeExecutionContext;

/**
 * General purpose evaluator for selection cool-downs in the StateTree.
 * - Add the cool-down evaluator to the state (and sub-state) that you want to guard
 * - Use the bActive in a condition to guard entering the state.
 */

UENUM()
enum class ECooldownGuardActivation : uint8
{
	OnEnterState,	// Activate cooldown when entering the state.
	OnCompleted,	// Activate cooldown when state is completed (either success or failure).
	OnFailed,		// Activate cooldown when state failed.
	OnSucceeded,	// Activate cooldown when state succeeded.
};

USTRUCT()
struct STATETREEMODULE_API FCooldownGuardEvaluatorInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter", Meta=(Tooltip = "Duration of the cool down."))
	float Duration = 5.0f;

	UPROPERTY()
	float EndTime = 0.0f;

	UPROPERTY(EditAnywhere, Category="Output", Meta=(Tooltip = "Cooldown is active."))
	bool bActive = false;
};

USTRUCT(DisplayName="Cooldown Guard")
struct STATETREEMODULE_API FCooldownGuardEvaluator : public FStateTreeEvaluatorCommonBase
{
	GENERATED_BODY()

	FCooldownGuardEvaluator() = default;
	virtual ~FCooldownGuardEvaluator() override {}

	virtual const UStruct* GetInstanceDataType() const override { return FCooldownGuardEvaluatorInstanceData::StaticStruct(); };
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const override;
	virtual void StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const override;

	void SetCoolDown(FStateTreeExecutionContext& Context) const;
	
	TStateTreeInstanceDataPropertyHandle<float> DurationHandle;
	TStateTreeInstanceDataPropertyHandle<float> EndTimeHandle;
	TStateTreeInstanceDataPropertyHandle<bool> ActiveHandle;

	UPROPERTY(EditAnywhere, Category="Default", Meta=(Tooltip = "Random deviation added to the duration."))
	float RandomDeviation = 0.0f;

	UPROPERTY(EditAnywhere, Category="Default", Meta=(Tooltip = "When to trigger the cooldown."))
	ECooldownGuardActivation Activation = ECooldownGuardActivation::OnCompleted;
};
