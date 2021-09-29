// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/MassZoneGraphFindWanderTarget.h"
#include "Templates/Tuple.h"
#include "StateTreeExecutionContext.h"
#include "MassStateTreeSubsystem.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphQuery.h"
#include "MassZoneGraphMovementFragments.h"
#include "MassAIBehaviorTypes.h"
#include "MassCrowdSettings.h"
#include "MassCrowdSubsystem.h"
#include "MassStateTreeExecutionContext.h"

FMassZoneGraphFindWanderTarget::FMassZoneGraphFindWanderTarget()
{
}

EStateTreeRunStatus FMassZoneGraphFindWanderTarget::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);

	const FMassZoneGraphLaneLocationFragment& LaneLocation = Context.GetExternalItem(LocationHandle).Get<FMassZoneGraphLaneLocationFragment>();
	const UZoneGraphSubsystem& ZoneGraphSubsystem = Context.GetExternalItem(ZoneGraphSubsystemHandle).Get<UZoneGraphSubsystem>();
	UZoneGraphAnnotationSubsystem& ZoneGraphAnnotationSubsystem = Context.GetExternalItem(ZoneGraphAnnotationSubsystemHandle).GetMutable<UZoneGraphAnnotationSubsystem>();
	const UMassCrowdSubsystem& MassCrowdSubsystem = Context.GetExternalItem(MassCrowdSubsystemHandle).Get<UMassCrowdSubsystem>();

	bool bDisplayDebug = false;
#if WITH_MASS_DEBUG
	bDisplayDebug = UE::MassDebug::IsDebuggingEntity(MassContext.GetEntity());
#endif // WITH_MASS_DEBUG

	if (!LaneLocation.LaneHandle.IsValid())
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Invalid lane location."));
		return EStateTreeRunStatus::Failed;
	}
			
	const FZoneGraphStorage* ZoneGraphStorage = ZoneGraphSubsystem.GetZoneGraphStorage(LaneLocation.LaneHandle.DataHandle);
	if (!ZoneGraphStorage)
	{
		MASSBEHAVIOR_LOG(Error, TEXT("Missing ZoneGraph Storage for current lane %s."), *LaneLocation.LaneHandle.ToString());
		return EStateTreeRunStatus::Failed;
	}

	const float MoveDistance = GetDefault<UMassCrowdSettings>()->GetMoveDistance();

	TargetLocation.LaneHandle = LaneLocation.LaneHandle;
	TargetLocation.TargetDistance = LaneLocation.DistanceAlongLane + MoveDistance;
	TargetLocation.NextExitLinkType = EZoneLaneLinkType::None;
	TargetLocation.NextLaneHandle.Reset();
	TargetLocation.bMoveReverse = false;
	TargetLocation.EndOfPathIntent = EMassMovementAction::Move;

	if (bDisplayDebug)
	{
		MASSBEHAVIOR_LOG(Log, TEXT("Find wander target."));
	}

	EStateTreeRunStatus Status = EStateTreeRunStatus::Running;
	
	// When close to end of a lane, choose next lane.
	if (TargetLocation.TargetDistance > LaneLocation.LaneLength)
	{
		TargetLocation.TargetDistance = FMath::Min(TargetLocation.TargetDistance, LaneLocation.LaneLength);

		typedef TTuple<const FZoneGraphLinkedLane, const float> FBranchingCandidate;
		TArray<FBranchingCandidate, TInlineAllocator<8>> Candidates;
		float CombinedWeight = 0.f;

		auto FindCandidates = [this, &ZoneGraphAnnotationSubsystem, &MassCrowdSubsystem, ZoneGraphStorage, LaneLocation, &Candidates, &CombinedWeight](const EZoneLaneLinkType Type)-> bool
		{
			TArray<FZoneGraphLinkedLane> LinkedLanes;
			UE::ZoneGraph::Query::GetLinkedLanes(*ZoneGraphStorage, LaneLocation.LaneHandle, Type, EZoneLaneLinkFlags::All, EZoneLaneLinkFlags::None, LinkedLanes);

			for (const FZoneGraphLinkedLane& LinkedLane : LinkedLanes)
			{
				// Apply tag filter
				const FZoneGraphTagMask BehaviorTags = ZoneGraphAnnotationSubsystem.GetAnnotationTags(LinkedLane.DestLane);
				if (!AllowedBehaviorTags.Pass(BehaviorTags))
				{
					continue;
				}

				// Add new candidate with its selection weight based on density
				const FZoneGraphTagMask& LaneTagMask = ZoneGraphStorage->Lanes[LinkedLane.DestLane.Index].Tags;
				const float Weight = MassCrowdSubsystem.GetDensityWeight(LinkedLane.DestLane, LaneTagMask);
				CombinedWeight += Weight;
				Candidates.Add(MakeTuple(LinkedLane, CombinedWeight));
			}

			return !Candidates.IsEmpty();
		};

		if (FindCandidates(EZoneLaneLinkType::Outgoing))
		{
			TargetLocation.NextExitLinkType = EZoneLaneLinkType::Outgoing;
		}
		else
		{
			// Could not continue, try to switch to an adjacent lane.
			// @todo: we could try to do something smarter here so that agents do not clump up. May need to have some heuristic,
			//		  i.e. at intersections it looks better to switch lane immediately, with flee, it looks better to vary the location randomly.
			TargetLocation.TargetDistance = LaneLocation.DistanceAlongLane;

			// Try adjacent lanes
			if (FindCandidates(EZoneLaneLinkType::Adjacent))
			{
				// Found adjacent lane, choose it once followed the short path. Keeping the random offset from above,
				// so that all agents dont follow until the end of the path to turn.
				TargetLocation.NextExitLinkType = EZoneLaneLinkType::Adjacent;
			}
		}

		if (Candidates.IsEmpty())
		{
			// Could not find next lane, fail.
			TargetLocation.Reset();
			Status = EStateTreeRunStatus::Failed;
		}
		else
		{
			// Select new lane based on the weight of each candidates
			const float Rand = FMath::RandRange(0.f, CombinedWeight);
			for (const FBranchingCandidate& Candidate : Candidates)
			{
				const float CandidateCombinedWeight = Candidate.Get<1>();
				if (Rand < CandidateCombinedWeight)
				{
					const FZoneGraphLinkedLane& LinkedLane = Candidate.Get<0>();
					TargetLocation.NextLaneHandle = LinkedLane.DestLane;
					break;
				}
			}
		}
	}

	WanderTargetLocation = &TargetLocation;
	
	return Status;
}

void FMassZoneGraphFindWanderTarget::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	// Temp solution to make sure the target location is always up to date (will be replaced with automatic update).
	WanderTargetLocation = &TargetLocation;
}

EStateTreeRunStatus FMassZoneGraphFindWanderTarget::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	// Temp solution to make sure the target location is always up to date (will be replaced with automatic update).
	WanderTargetLocation = &TargetLocation;
	
	return EStateTreeRunStatus::Running;
}
