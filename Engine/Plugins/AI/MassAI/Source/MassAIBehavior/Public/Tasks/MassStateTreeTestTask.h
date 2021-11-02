// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "MassStateTreeTestTask.generated.h"

class UMassStateTreeSubsystem;
struct FMassSmartObjectUserFragment;
struct FDataFragment_Transform;

/**
 * Test Task, will be removed later.
 */
USTRUCT(meta = (DisplayName = "Mass Test Task"))
struct MASSAIBEHAVIOR_API FMassStateTreeTestTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassStateTreeTestTask();
	virtual ~FMassStateTreeTestTask();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	TStateTreeItemHandle<UMassStateTreeSubsystem> MassStateTreeSubSystemHandle;
	TStateTreeItemHandle<FMassSmartObjectUserFragment> SmartObjectUserHandle;
	TStateTreeItemHandle<FDataFragment_Transform> TransformHandle;

	UPROPERTY()
	float Time = 0.0f;

	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	float Duration = 5.0f;

};
