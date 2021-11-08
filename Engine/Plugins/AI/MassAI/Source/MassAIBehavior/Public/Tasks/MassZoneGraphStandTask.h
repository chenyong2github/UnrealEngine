// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassStateTreeTypes.h"
#include "MassMovementTypes.h"
#include "MassZoneGraphStandTask.generated.h"

struct FStateTreeExecutionContext;
struct FMassZoneGraphLaneLocationFragment;
struct FMassMoveTargetFragment;
struct FMassZoneGraphShortPathFragment;
struct FMassZoneGraphCachedLaneFragment;
class UZoneGraphSubsystem;
class UMassSignalSubsystem;
struct FMassMovementConfigFragment;

/**
 * Stop, and stand on current ZoneGraph location
 */
USTRUCT(meta = (DisplayName = "ZG Stand"))
struct MASSAIBEHAVIOR_API FMassZoneGraphStandTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	TStateTreeItemHandle<FMassZoneGraphLaneLocationFragment> LocationHandle;
	TStateTreeItemHandle<FMassMoveTargetFragment> MoveTargetHandle;
	TStateTreeItemHandle<FMassZoneGraphShortPathFragment> ShortPathHandle;
	TStateTreeItemHandle<FMassZoneGraphCachedLaneFragment> CachedLaneHandle;
	TStateTreeItemHandle<UZoneGraphSubsystem> ZoneGraphSubsystemHandle;
	TStateTreeItemHandle<UMassSignalSubsystem> MassSignalSubsystemHandle;
	TStateTreeItemHandle<FMassMovementConfigFragment> MovementConfigHandle;

	UPROPERTY()
	float Time = 0.0f;

	/** Delay before the task ends. Default (0 or any negative) will run indefinitely so it requires a transition in the state tree to stop it. */
	UPROPERTY(EditAnywhere, Category = Parameters, meta = (Bindable))
	float Duration = 0.0f;
};
