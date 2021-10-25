// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "MassMovementTypes.h"
#include "MassZoneGraphStandTask.generated.h"

struct FStateTreeExecutionContext;

/**
 * Stop, and stand on current ZoneGraph location
 */
USTRUCT(meta = (DisplayName = "ZG Stand"))
struct MASSAIBEHAVIOR_API FMassZoneGraphStandTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphLaneLocationFragment"))
	FStateTreeExternalItemHandle LocationHandle;

	UPROPERTY(meta=(BaseStruct="MassMoveTargetFragment"))
	FStateTreeExternalItemHandle MoveTargetHandle;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphShortPathFragment"))
	FStateTreeExternalItemHandle ShortPathHandle;

	UPROPERTY(meta=(BaseStruct="MassZoneGraphCachedLaneFragment"))
	FStateTreeExternalItemHandle CachedLaneHandle;

	UPROPERTY(meta=(BaseClass="ZoneGraphSubsystem"))
	FStateTreeExternalItemHandle ZoneGraphSubsystemHandle;

	UPROPERTY(meta=(BaseClass="MassSignalSubsystem"))
	FStateTreeExternalItemHandle MassSignalSubsystemHandle;

	UPROPERTY(meta=(BaseStruct="MassMovementConfigFragment"))
	FStateTreeExternalItemHandle MovementConfigHandle;

	UPROPERTY()
	float Time = 0.0f;

	/** Delay before the task ends. Default (0 or any negative) will run indefinitely so it requires a transition in the state tree to stop it. */
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (Bindable))
	float Duration = 0.0f;
};
