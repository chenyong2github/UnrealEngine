// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "MassZoneGraphFindEscapeTarget.generated.h"

struct FStateTreeExecutionContext;

/**
 * Updates TargetLocation to a escape target based on the agents current location on ZoneGraph, and disturbance annotation.
 */
USTRUCT(meta = (DisplayName = "ZG Find Escape Target"))
struct MASSAIBEHAVIOR_API FMassZoneGraphFindEscapeTarget : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

	FMassZoneGraphFindEscapeTarget();

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphLaneLocationFragment"))
	FStateTreeExternalItemHandle LocationHandle;

	UPROPERTY(meta=(BaseStruct="MassMoveTargetFragment"))
	FStateTreeExternalItemHandle MoveTargetHandle;

	UPROPERTY(meta=(BaseClass="ZoneGraphSubsystem"))
	FStateTreeExternalItemHandle ZoneGraphSubsystemHandle;

	UPROPERTY(meta=(BaseClass="ZoneGraphAnnotationSubsystem"))
	FStateTreeExternalItemHandle ZoneGraphAnnotationSubsystemHandle;

	UPROPERTY(EditAnywhere, Category = Parameters)
	FZoneGraphTag DisturbanceAnnotationTag = FZoneGraphTag::None;

	FMassZoneGraphTargetLocation TargetLocation;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation"))
	FStateTreeResultRef EscapeTargetLocation;
};
