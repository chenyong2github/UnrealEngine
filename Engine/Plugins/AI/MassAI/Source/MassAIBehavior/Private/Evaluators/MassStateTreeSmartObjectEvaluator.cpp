// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/MassStateTreeSmartObjectEvaluator.h"

#include "MassAIBehaviorTypes.h"
#include "MassCommonFragments.h"
#include "MassSignalSubsystem.h"
#include "MassSmartObjectFragments.h"
#include "MassSmartObjectHandler.h"
#include "MassStateTreeExecutionContext.h"
#include "MassZoneGraphNavigationFragments.h"
#include "SmartObjectSubsystem.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// FMassStateTreeSmartObjectEvaluator
//----------------------------------------------------------------------//

bool FMassStateTreeSmartObjectEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	Linker.LinkExternalData(MassSignalSubsystemHandle);
	Linker.LinkExternalData(EntityTransformHandle);
	Linker.LinkExternalData(SmartObjectUserHandle);
	Linker.LinkExternalData(LocationHandle);

	Linker.LinkInstanceDataProperty(SearchRequestResultHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassStateTreeSmartObjectEvaluatorInstanceData, SearchRequestResult));
	Linker.LinkInstanceDataProperty(SearchRequestIDHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassStateTreeSmartObjectEvaluatorInstanceData, SearchRequestID));
	Linker.LinkInstanceDataProperty(CandidatesFoundHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassStateTreeSmartObjectEvaluatorInstanceData, bCandidatesFound));
	Linker.LinkInstanceDataProperty(ClaimedHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassStateTreeSmartObjectEvaluatorInstanceData, bClaimed));
	Linker.LinkInstanceDataProperty(NextUpdateHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassStateTreeSmartObjectEvaluatorInstanceData, NextUpdate));
	Linker.LinkInstanceDataProperty(UsingZoneGraphAnnotationsHandle, STATETREE_INSTANCEDATA_PROPERTY(FMassStateTreeSmartObjectEvaluatorInstanceData, bUsingZoneGraphAnnotations));

	return true;
}

void FMassStateTreeSmartObjectEvaluator::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (ChangeType != EStateTreeStateChangeType::Changed)
	{
		return;
	}

	Reset(Context);
}

void FMassStateTreeSmartObjectEvaluator::Reset(FStateTreeExecutionContext& Context) const
{
	bool& bCandidatesFound = Context.GetInstanceData(CandidatesFoundHandle);
	bool& bClaimed = Context.GetInstanceData(ClaimedHandle);
	FMassSmartObjectRequestID& SearchRequestID = Context.GetInstanceData(SearchRequestIDHandle);
	if (SearchRequestID.IsSet())
	{
		const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
		USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
		UMassSignalSubsystem& SignalSubsystem = Context.GetExternalData(MassSignalSubsystemHandle);
		const FMassSmartObjectHandler MassSmartObjectHandler(
			MassContext.GetEntitySubsystem(),
			MassContext.GetEntitySubsystemExecutionContext(),
			SmartObjectSubsystem,
			SignalSubsystem);
		MassSmartObjectHandler.RemoveRequest(SearchRequestID);
		SearchRequestID.Reset();
	}

	bCandidatesFound = false;
	bClaimed = false;
}

void FMassStateTreeSmartObjectEvaluator::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType,  const float DeltaTime) const
{
	const FMassSmartObjectUserFragment& SOUser = Context.GetExternalData(SmartObjectUserHandle);

	bool& bCandidatesFound = Context.GetInstanceData(CandidatesFoundHandle);
	bool& bClaimed = Context.GetInstanceData(ClaimedHandle);
	FMassSmartObjectRequestID& SearchRequestID = Context.GetInstanceData(SearchRequestIDHandle);

	bCandidatesFound = false;
	bClaimed = SOUser.ClaimHandle.IsValid();

	// Already claimed, nothing to do
	if (bClaimed)
	{
		MASSBEHAVIOR_LOG(Verbose, TEXT("Skipped: currently claimed"));
		return;
	}

	const UWorld* World = Context.GetWorld();
	if (SOUser.CooldownEndTime > World->GetTimeSeconds())
	{
		MASSBEHAVIOR_LOG(Verbose, TEXT("Skipped: cooldown active (%s)"), *LexToString(SOUser.CooldownEndTime - World->GetTimeSeconds()));
		return;
	}

	// We need to track our next update cooldown since we can get ticked from any signals waking up the StateTree
	float& NextUpdate = Context.GetInstanceData(NextUpdateHandle);
	if (NextUpdate > World->GetTimeSeconds())
	{
		MASSBEHAVIOR_LOG(Verbose, TEXT("Skipped: waiting next update time (%s)"), *LexToString(NextUpdate-World->GetTimeSeconds()));
		return;
	}
	NextUpdate = 0.f;

	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	UMassSignalSubsystem& SignalSubsystem = Context.GetExternalData(MassSignalSubsystemHandle);
	const FMassStateTreeExecutionContext& MassContext = static_cast<FMassStateTreeExecutionContext&>(Context);
	const FMassSmartObjectHandler MassSmartObjectHandler(
		MassContext.GetEntitySubsystem(),
		MassContext.GetEntitySubsystemExecutionContext(),
		SmartObjectSubsystem,
		SignalSubsystem);

	// Nothing claimed -> search for candidates
	bool& bUsingZoneGraphAnnotations = Context.GetInstanceData(UsingZoneGraphAnnotationsHandle);
	if (!SearchRequestID.IsSet())
	{
		// Use lanes if possible for faster queries using zone graph annotations
		const FMassEntityHandle RequestingEntity = MassContext.GetEntity();
		const FMassZoneGraphLaneLocationFragment* LaneLocation = Context.GetExternalDataPtr(LocationHandle);
		bUsingZoneGraphAnnotations = LaneLocation != nullptr;
		if (bUsingZoneGraphAnnotations)
		{
			MASSBEHAVIOR_CLOG(!LaneLocation->LaneHandle.IsValid(), Error, TEXT("Always expecting a valid lane from the ZoneGraph movement"));
			if (LaneLocation->LaneHandle.IsValid())
			{
				MASSBEHAVIOR_LOG(Log, TEXT("Requesting search candidates from lane %s (%s/%s)"),
					*LaneLocation->LaneHandle.ToString(),
					*LexToString(LaneLocation->DistanceAlongLane),
					*LexToString(LaneLocation->LaneLength));
				SearchRequestID = MassSmartObjectHandler.FindCandidatesAsync(RequestingEntity, { LaneLocation->LaneHandle, LaneLocation->DistanceAlongLane });
			}
		}
		else
		{
			const FTransformFragment& TransformFragment = Context.GetExternalData(EntityTransformHandle);
			SearchRequestID = MassSmartObjectHandler.FindCandidatesAsync(RequestingEntity, TransformFragment.GetTransform().GetLocation());
		}
	}
	else
	{
		// Fetch request results
		FMassSmartObjectRequestResult& SearchRequestResult = Context.GetInstanceData(SearchRequestResultHandle);
		SearchRequestResult = MassSmartObjectHandler.GetRequestResult(SearchRequestID);

		// Check if results are ready
		if (SearchRequestResult.bProcessed)
		{
			// Remove requests
			MassSmartObjectHandler.RemoveRequest(SearchRequestID);
			SearchRequestID.Reset();

			// Update bindable flag to indicate to tasks and conditions if some candidates were found
			bCandidatesFound = SearchRequestResult.NumCandidates > 0;

			MASSBEHAVIOR_CLOG(bCandidatesFound, Log, TEXT("Found %d smart object candidates"), SearchRequestResult.NumCandidates);

			// When using ZoneGraph annotations we don't need to schedule a new update since we only need the CurrentLaneChanged signal.
			// Otherwise we reschedule with default interval on success or retry interval on failed attempt
			if (!bUsingZoneGraphAnnotations)
			{
				const float DelayInSeconds = bCandidatesFound ? TickInterval : RetryCooldown;
				NextUpdate = World->GetTimeSeconds() + DelayInSeconds;
				UMassSignalSubsystem& MassSignalSubsystem = Context.GetExternalData(MassSignalSubsystemHandle);
				MassSignalSubsystem.DelaySignalEntity(UE::Mass::Signals::SmartObjectRequestCandidates, MassContext.GetEntity(), DelayInSeconds);
			}
		}
		// else wait for the Evaluation that will be triggered by the "candidates ready" signal
	}
}
