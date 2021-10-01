// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassZoneGraphPathFollowTask.h"

#include "MassAIBehaviorTypes.h"
#include "MassCommonFragments.h"
#include "MassAIMovementFragments.h"
#include "MassMovementSettings.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphMovementFragments.h"
#include "MassZoneGraphMovementUtils.h"
#include "ZoneGraphSubsystem.h"

bool FMassZoneGraphPathFollowTask::RequestPath(FMassStateTreeExecutionContext& Context, const FMassZoneGraphTargetLocation& RequestedTargetLocation) const
{
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetExternalItem(ZoneGraphSubsystemHandle).Get<UZoneGraphSubsystem>();
	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalItem(LocationHandle).Get<FMassZoneGraphLaneLocationFragment>();
	const FMassMovementConfigFragment& MovementConfig = Context.GetExternalItem(MovementConfigHandle).Get<FMassMovementConfigFragment>();
	const FDataFragment_AgentRadius& AgentRadius = Context.GetExternalItem(AgentRadiusHandle).Get<FDataFragment_AgentRadius>();
	FMassZoneGraphShortPathFragment& ShortPath = Context.GetExternalItem(ShortPathHandle).GetMutable<FMassZoneGraphShortPathFragment>();
	FMassZoneGraphCachedLaneFragment& CachedLane = Context.GetExternalItem(CachedLaneHandle).GetMutable<FMassZoneGraphCachedLaneFragment>();
	FMassMoveTargetFragment& MoveTarget = Context.GetExternalItem(MoveTargetHandle).GetMutable<FMassMoveTargetFragment>();
	FMassZoneGraphPathRequestFragment& RequestFragment = Context.GetExternalItem(PathRequestHandle).GetMutable<FMassZoneGraphPathRequestFragment>();

	const UMassMovementSettings* Settings = GetDefault<UMassMovementSettings>();
	check(Settings);

	const FMassMovementConfig* Config = Settings->GetMovementConfigByHandle(MovementConfig.ConfigHandle);
	if (!Config)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Failed to get move config %s."), *LaneLocation.LaneHandle.ToString());
		return false;
	}

	bool bDisplayDebug = false;
#if WITH_MASSGAMEPLAY_DEBUG
	bDisplayDebug = UE::Mass::Debug::IsDebuggingEntity(Context.GetEntity());
#endif // WITH_MASSGAMEPLAY_DEBUG

	if (RequestedTargetLocation.LaneHandle != LaneLocation.LaneHandle)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Target location lane %s does not match current lane location %s."),
			*RequestedTargetLocation.LaneHandle.ToString(), *LaneLocation.LaneHandle.ToString());
		return false;
	}

	if (bDisplayDebug)
	{
		MASSBEHAVIOR_LOG(Log, TEXT("PathFollow Request: %s, lane %s, Start: %f End:%f, next lane %s."),
			RequestedTargetLocation.bMoveReverse ? TEXT("reverse") : TEXT("forward"),
			*LaneLocation.LaneHandle.ToString(),
			LaneLocation.DistanceAlongLane, RequestedTargetLocation.TargetDistance,
			*RequestedTargetLocation.NextLaneHandle.ToString());
	}

	// @todo: Combine FMassZoneGraphTargetLocation and FZoneGraphShortPathRequest.
	FZoneGraphShortPathRequest& PathRequest = RequestFragment.PathRequest;
	PathRequest.StartPosition = MoveTarget.Center;
	PathRequest.bMoveReverse = RequestedTargetLocation.bMoveReverse;
	PathRequest.TargetDistance = RequestedTargetLocation.TargetDistance;
	PathRequest.NextLaneHandle = RequestedTargetLocation.NextLaneHandle;
	PathRequest.NextExitLinkType = RequestedTargetLocation.NextExitLinkType;
	PathRequest.EndOfPathIntent = RequestedTargetLocation.EndOfPathIntent;
	PathRequest.bIsEndOfPathPositionSet = RequestedTargetLocation.EndOfPathPosition.IsSet();
	PathRequest.EndOfPathPosition = RequestedTargetLocation.EndOfPathPosition.Get(FVector::ZeroVector);
	PathRequest.bIsEndOfPathDirectionSet = RequestedTargetLocation.EndOfPathDirection.IsSet();
	PathRequest.EndOfPathDirection.Set(RequestedTargetLocation.EndOfPathDirection.Get(FVector::ForwardVector));
	PathRequest.AnticipationDistance = RequestedTargetLocation.AnticipationDistance;
	PathRequest.EndOfPathOffset.Set(FMath::RandRange(-AgentRadius.Radius, AgentRadius.Radius));

	const float DesiredSpeed = FMath::Min(Config->GenerateDesiredSpeed(MovementStyle, Context.GetEntity().Index) * SpeedScale, Config->MaximumSpeed);
	const UWorld* World = Context.GetWorld();
	checkf(World != nullptr, TEXT("A valid world is expected from the execution context"));

	MoveTarget.CreateNewAction(EMassMovementAction::Move, *World);
	return UE::MassMovement::ActivateActionMove(*World, Context.GetOwner(), Context.GetEntity(), ZoneGraphSubsystem, LaneLocation, PathRequest, AgentRadius.Radius, DesiredSpeed, MoveTarget, ShortPath, CachedLane);
}

EStateTreeRunStatus FMassZoneGraphPathFollowTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	// Do not reset of the state if current state is still active after transition, unless transitioned specifically to this state.
	if (ChangeType == EStateTreeStateChangeType::Sustained && Transition.Current != Transition.Next)
	{
		return EStateTreeRunStatus::Running;
	}
	
	FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);

	bool bDisplayDebug = false;
#if WITH_MASSGAMEPLAY_DEBUG
	bDisplayDebug = UE::Mass::Debug::IsDebuggingEntity(MassContext.GetEntity());
#endif // WITH_MASSGAMEPLAY_DEBUG
	if (bDisplayDebug)
	{
		MASSBEHAVIOR_LOG(Verbose, TEXT("enterstate."));
	}

	if (Context.GetEnterStateStatus() == EStateTreeRunStatus::Failed)
	{
		MASSBEHAVIOR_LOG(Log, TEXT("Transition to the state has been denied by other tasks. Nothing to do."));
		return EStateTreeRunStatus::Failed;
	}

	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalItem(LocationHandle).Get<FMassZoneGraphLaneLocationFragment>();

	const FMassZoneGraphTargetLocation* TargetLocationPtr = TargetLocation.GetPtr<FMassZoneGraphTargetLocation>();
	if (!TargetLocationPtr)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Target location not set."));
		return EStateTreeRunStatus::Failed;
	}
	
	if (TargetLocationPtr->LaneHandle != LaneLocation.LaneHandle)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Target is not on current lane, target lane is %s expected %s."), *TargetLocationPtr->LaneHandle.ToString(), *LaneLocation.LaneHandle.ToString());
		return EStateTreeRunStatus::Failed;
	}

	if (!RequestPath(MassContext, *TargetLocationPtr))
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FMassZoneGraphPathFollowTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);

	bool bDisplayDebug = false;
#if WITH_MASSGAMEPLAY_DEBUG
	bDisplayDebug = UE::Mass::Debug::IsDebuggingEntity(MassContext.GetEntity());
#endif // WITH_MASSGAMEPLAY_DEBUG
	if (bDisplayDebug)
	{
		MASSBEHAVIOR_LOG(Verbose, TEXT("tick"));
	}

	const FMassZoneGraphShortPathFragment& ShortPath = Context.GetExternalItem(ShortPathHandle).Get<FMassZoneGraphShortPathFragment>();
	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalItem(LocationHandle).Get<FMassZoneGraphLaneLocationFragment>();

	const FMassZoneGraphTargetLocation* TargetLocationPtr = TargetLocation.GetPtr<FMassZoneGraphTargetLocation>();
	if (!TargetLocationPtr)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Target location not set."));
		return EStateTreeRunStatus::Failed;
	}

	// Current path follow is done, but it was partial (i.e. many points on a curve), try again until we get there.
	if (ShortPath.IsDone() && ShortPath.bPartialResult)
	{
		if (TargetLocationPtr->LaneHandle != LaneLocation.LaneHandle)
		{
			MASSBEHAVIOR_LOG(Error, TEXT("Target is not on current lane, target lane is %s expected %s."), *TargetLocationPtr->LaneHandle.ToString(), *LaneLocation.LaneHandle.ToString());
			return EStateTreeRunStatus::Failed;
		}
		
		if (!RequestPath(MassContext, *TargetLocationPtr))
		{
			MASSBEHAVIOR_LOG(Error, TEXT("Failed to request path."));
			return EStateTreeRunStatus::Failed;
		}
	}
	
	return ShortPath.IsDone() ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running;
}
