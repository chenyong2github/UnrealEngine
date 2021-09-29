// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "ZoneGraphTypes.h"
#include "Tasks/MassZoneGraphPathFollowTask.h"
#include "MassCrowdClaimWaitSlotTask.generated.h"

struct FStateTreeExecutionContext;

/**
* Claim wait slot and expose slot position for path follow.
*/
USTRUCT(meta = (DisplayName = "Crowd Claim Wait Slot"))
struct MASSCROWD_API FMassCrowdClaimWaitSlotTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphLaneLocationFragment"))
	FStateTreeExternalItemHandle LocationHandle;

	UPROPERTY(meta=(BaseStruct="MassMoveTargetFragment"))
	FStateTreeExternalItemHandle MoveTargetHandle;

	UPROPERTY(meta=(BaseClass="MassCrowdSubsystem"))
	FStateTreeExternalItemHandle CrowdSubsystemHandle;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation"))
	FStateTreeResultRef WaitSlotLocation;

	FMassZoneGraphTargetLocation TargetLocation;

	int32 WaitingSlotIndex = INDEX_NONE;
	
	FZoneGraphLaneHandle AcquiredLaneHandle;
};
