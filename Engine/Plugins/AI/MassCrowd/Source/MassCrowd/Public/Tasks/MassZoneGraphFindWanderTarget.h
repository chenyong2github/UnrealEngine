// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "MassZoneGraphFindWanderTarget.generated.h"

struct FStateTreeExecutionContext;
struct FMassZoneGraphLaneLocationFragment;
class UZoneGraphSubsystem;
class UZoneGraphAnnotationSubsystem;
class UMassCrowdSubsystem;

/**
 * Updates TargetLocation to a wander target based on the agents current location on ZoneGraph.
 */
USTRUCT(meta = (DisplayName = "ZG Find Wander Target"))
struct MASSCROWD_API FMassZoneGraphFindWanderTarget : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassZoneGraphFindWanderTarget();

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	TStateTreeItemHandle<FMassZoneGraphLaneLocationFragment> LocationHandle;
	TStateTreeItemHandle<UZoneGraphSubsystem> ZoneGraphSubsystemHandle;
	TStateTreeItemHandle<UZoneGraphAnnotationSubsystem> ZoneGraphAnnotationSubsystemHandle;
	TStateTreeItemHandle<UMassCrowdSubsystem> MassCrowdSubsystemHandle;

	UPROPERTY(EditAnywhere, Category = Parameters)
	FZoneGraphTagFilter AllowedBehaviorTags;

	FMassZoneGraphTargetLocation TargetLocation;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation"))
	FStateTreeResultRef WanderTargetLocation;
};
