// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassAIMovementTypes.h"
#include "MassCommonTypes.h"
#include "MassStateTreeTypes.h"
#include "ZoneGraphTypes.h"
#include "MassZoneGraphPathFollowTask.generated.h"

struct FMassStateTreeExecutionContext;
struct FMassZoneGraphLaneLocationFragment;
struct FMassMoveTargetFragment;
struct FMassMovementConfigFragment;
struct FMassZoneGraphPathRequestFragment;
struct FMassZoneGraphShortPathFragment;
struct FMassZoneGraphCachedLaneFragment;
struct FDataFragment_AgentRadius;
class UZoneGraphSubsystem;

USTRUCT()
struct MASSAIBEHAVIOR_API FMassZoneGraphTargetLocation : public FStateTreeResult
{
	GENERATED_BODY()

	virtual const UScriptStruct& GetStruct() const override { return *StaticStruct(); }

	void Reset()
	{
		LaneHandle.Reset();
		NextLaneHandle.Reset();
		NextExitLinkType = EZoneLaneLinkType::None;
		bMoveReverse = false;
		TargetDistance = 0.0f;
		EndOfPathPosition.Reset();
		AnticipationDistance.Set(50.0f);
		EndOfPathIntent = EMassMovementAction::Move;
	}

	/** Current lane handle. (Could be debug only) */
	FZoneGraphLaneHandle LaneHandle;
	
	/** If valid, the this lane will be set as current lane after the path follow is completed. */
	FZoneGraphLaneHandle NextLaneHandle;
	
	/** Target distance along current lane. */
	float TargetDistance = 0.0f;
	
	/** Optional end of path location. */
	TOptional<FVector> EndOfPathPosition;

	/** Optional end of path direction, used only if EndOfPathPosition is set. */
	TOptional<FVector> EndOfPathDirection;

	/** If start or end of path is off-lane, the distance along the lane is pushed forward/back along the lane to make smoother transition. */
	FMassInt16Real AnticipationDistance = FMassInt16Real(50.0f);

	/** True, if we're moving reverse along the lane. */
	bool bMoveReverse = false;
	
	/** Movement intent at the end of the path */
	EMassMovementAction EndOfPathIntent = EMassMovementAction::Move;
	
	/** How the next lane handle is reached relative to the current lane. */
	EZoneLaneLinkType NextExitLinkType = EZoneLaneLinkType::None;
};

/**
 * Follows a path long the current lane to a specified point.
 */
USTRUCT(meta = (DisplayName = "ZG Path Follow"))
struct MASSAIBEHAVIOR_API FMassZoneGraphPathFollowTask : public FMassStateTreeTaskBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	bool RequestPath(FMassStateTreeExecutionContext& Context, const FMassZoneGraphTargetLocation& TargetLocation) const;

	TStateTreeItemHandle<FMassZoneGraphLaneLocationFragment> LocationHandle;
	TStateTreeItemHandle<FMassMoveTargetFragment> MoveTargetHandle;
	TStateTreeItemHandle<FMassMovementConfigFragment> MovementConfigHandle;
	TStateTreeItemHandle<FMassZoneGraphPathRequestFragment> PathRequestHandle;
	TStateTreeItemHandle<FMassZoneGraphShortPathFragment> ShortPathHandle;
	TStateTreeItemHandle<FMassZoneGraphCachedLaneFragment> CachedLaneHandle;
	TStateTreeItemHandle<FDataFragment_AgentRadius> AgentRadiusHandle;
	TStateTreeItemHandle<UZoneGraphSubsystem> ZoneGraphSubsystemHandle;

	UPROPERTY(EditAnywhere, Category = Parameters, meta=(Struct="MassZoneGraphTargetLocation", Bindable))
	FStateTreeResultRef TargetLocation;

	UPROPERTY(EditAnywhere, Category = Parameters)
	FMassMovementStyleRef MovementStyle;

	UPROPERTY(EditAnywhere, Category = Parameters)
	float SpeedScale = 1.0f;
};
