// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "MassStateTreeTestTask.generated.h"

/**
 * Test Task, will be removed later.
 */
USTRUCT(meta = (DisplayName = "Mass Test Task"))
struct MASSAIBEHAVIOR_API FMassStateTreeTestTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassStateTreeTestTask();
	virtual ~FMassStateTreeTestTask();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

protected:
	UPROPERTY()
	float Time = 0.0f;

	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	float Duration = 5.0f;

	UPROPERTY(meta=(BaseClass="MassStateTreeSubsystem"))
	FStateTreeExternalItemHandle MassStateTreeSubSystemHandle;

	UPROPERTY(meta=(BaseStruct="DataFragment_SmartObjectUser", Optional))
	FStateTreeExternalItemHandle SmartObjectUserHandle;

	UPROPERTY(meta=(BaseStruct="DataFragment_Transform"))
	FStateTreeExternalItemHandle TransformHandle;
};
