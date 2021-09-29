// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "MassZoneGraphFindWanderTarget.generated.h"

struct FStateTreeExecutionContext;

/**
 * Updates TargetLocation to a wander target based on the agents current location on ZoneGraph.
 */
USTRUCT(meta = (DisplayName = "ZG Find Wander Target"))
struct MASSCROWD_API FMassZoneGraphFindWanderTarget : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassZoneGraphFindWanderTarget();

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphLaneLocationFragment"))
	FStateTreeExternalItemHandle LocationHandle;

	UPROPERTY(meta=(BaseClass="ZoneGraphSubsystem"))
	FStateTreeExternalItemHandle ZoneGraphSubsystemHandle;

	UPROPERTY(meta=(BaseClass="ZoneGraphAnnotationSubsystem"))
	FStateTreeExternalItemHandle ZoneGraphAnnotationSubsystemHandle;

	UPROPERTY(meta=(BaseClass="MassCrowdSubsystem"))
	FStateTreeExternalItemHandle MassCrowdSubsystemHandle;

	UPROPERTY(EditAnywhere, Category = Parameters)
	FZoneGraphTagFilter AllowedBehaviorTags;

	FMassZoneGraphTargetLocation TargetLocation;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation"))
	FStateTreeResultRef WanderTargetLocation;
};
