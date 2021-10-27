// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "MassZoneGraphFindEscapeTarget.generated.h"

struct FStateTreeExecutionContext;
struct FMassZoneGraphLaneLocationFragment;
class UZoneGraphSubsystem;
class UZoneGraphAnnotationSubsystem;

/**
 * Updates TargetLocation to a escape target based on the agents current location on ZoneGraph, and disturbance annotation.
 */
USTRUCT(meta = (DisplayName = "ZG Find Escape Target"))
struct MASSAIBEHAVIOR_API FMassZoneGraphFindEscapeTarget : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassZoneGraphFindEscapeTarget();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	TStateTreeItemHandle<FMassZoneGraphLaneLocationFragment> LocationHandle;
	TStateTreeItemHandle<UZoneGraphSubsystem> ZoneGraphSubsystemHandle;
	TStateTreeItemHandle<UZoneGraphAnnotationSubsystem> ZoneGraphAnnotationSubsystemHandle;

	UPROPERTY(EditAnywhere, Category = Parameters)
	FZoneGraphTag DisturbanceAnnotationTag = FZoneGraphTag::None;

	FMassZoneGraphTargetLocation TargetLocation;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation"))
	FStateTreeResultRef EscapeTargetLocation;
};
