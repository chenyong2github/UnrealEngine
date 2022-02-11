// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassSmartObjectHandler.h"
#include "MassSmartObjectBehaviorDefinition.h"
#include "MassCommonTypes.h"
#include "SmartObjectSubsystem.h"
#include "VisualLogger/VisualLogger.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassSignalSubsystem.h"
#include "MassSmartObjectFragments.h"
#include "Engine/World.h"

namespace UE::Mass::SmartObject
{
	void Release(FMassCommandBuffer& CommandBuffer, const FMassBehaviorEntityContext& Context, const EMassSmartObjectInteractionStatus NewStatus)
	{
		FMassSmartObjectUserFragment& User = Context.EntityView.GetFragmentData<FMassSmartObjectUserFragment>();

		if (User.InteractionStatus == EMassSmartObjectInteractionStatus::InProgress ||
			User.InteractionStatus == EMassSmartObjectInteractionStatus::BehaviorCompleted)
		{
			if (const USmartObjectMassBehaviorDefinition* BehaviorDefinition = Context.SmartObjectSubsystem.GetBehaviorDefinition<USmartObjectMassBehaviorDefinition>(User.ClaimHandle))
			{
				BehaviorDefinition->Deactivate(CommandBuffer, Context);
			}
		}
		Context.SmartObjectSubsystem.Release(User.ClaimHandle);

		User.InteractionStatus = NewStatus;
		User.ClaimHandle.Invalidate();
	}

	struct FPayload
	{
		FMassEntityHandle Entity;
		TWeakObjectPtr<UMassEntitySubsystem> EntitySubsystem;
		TWeakObjectPtr<USmartObjectSubsystem> SmartObjectSubsystem;
		TWeakObjectPtr<UMassSignalSubsystem> SignalSubsystem;
	};

	void OnSlotInvalidated(const FSmartObjectClaimHandle& ClaimHandle, const ESmartObjectSlotState State, FPayload Payload)
	{
		USmartObjectSubsystem* SmartObjectSubsystem = Payload.SmartObjectSubsystem.Get();
		UMassEntitySubsystem* EntitySubsystem = Payload.EntitySubsystem.Get();
		UMassSignalSubsystem* SignalSubsystem = Payload.SignalSubsystem.Get();
		if (SmartObjectSubsystem != nullptr && EntitySubsystem != nullptr && SignalSubsystem != nullptr && EntitySubsystem->IsEntityActive(Payload.Entity))
		{
			const FMassEntityView EntityView(*EntitySubsystem, Payload.Entity);
			const FMassBehaviorEntityContext Context(EntityView, *SmartObjectSubsystem);

			Release(EntitySubsystem->Defer(), Context, EMassSmartObjectInteractionStatus::Aborted);
			if (EntitySubsystem->IsProcessing() == false)
			{
				EntitySubsystem->FlushCommands();
			}

			SignalSubsystem->SignalEntity(UE::Mass::Signals::SmartObjectInteractionDone, Payload.Entity);
		}
	}
} // UE::Mass::SmartObject;

//----------------------------------------------------------------------//
// FMassSmartObjectHandler
//----------------------------------------------------------------------//
FMassSmartObjectRequestID FMassSmartObjectHandler::FindCandidatesAsync(const FMassEntityHandle RequestingEntity, const FVector& Location) const
{
	const FMassEntityHandle ReservedEntity = EntitySubsystem.ReserveEntity();

	FMassSmartObjectWorldLocationRequestFragment RequestFragment;
	RequestFragment.SearchOrigin = Location;
	RequestFragment.RequestingEntity = RequestingEntity;

	FMassSmartObjectRequestResultFragment ResultFragment;

	ExecutionContext.Defer().PushCommand(
		FBuildEntityFromFragmentInstances(ReservedEntity,
			{
				FStructView::Make(RequestFragment),
				FStructView::Make(ResultFragment)
			}));

	return FMassSmartObjectRequestID(ReservedEntity);
}

FMassSmartObjectRequestID FMassSmartObjectHandler::FindCandidatesAsync(const FMassEntityHandle RequestingEntity, const FZoneGraphCompactLaneLocation& LaneLocation) const
{
	const FMassEntityHandle ReservedEntity = EntitySubsystem.ReserveEntity();

	FMassSmartObjectLaneLocationRequestFragment RequestFragment;
	RequestFragment.CompactLaneLocation = LaneLocation;
	RequestFragment.RequestingEntity = RequestingEntity;

	FMassSmartObjectRequestResultFragment ResultFragment;

	ExecutionContext.Defer().PushCommand(
		FBuildEntityFromFragmentInstances(ReservedEntity,
			{
				FStructView::Make(RequestFragment),
				FStructView::Make(ResultFragment)
			}));

	return FMassSmartObjectRequestID(ReservedEntity);
}

FMassSmartObjectRequestResult FMassSmartObjectHandler::GetRequestResult(const FMassSmartObjectRequestID& RequestID) const
{
	const FMassEntityHandle RequestEntity = static_cast<FMassEntityHandle>(RequestID);
	if (!ensureMsgf(EntitySubsystem.IsEntityValid(RequestEntity), TEXT("Invalid request.")))
	{
		return {};
	}

	// Check if entity is built by now.
	if (!EntitySubsystem.IsEntityBuilt(RequestEntity))
	{
		return {};
	}

	const FMassSmartObjectRequestResultFragment& RequestFragment = EntitySubsystem.GetFragmentDataChecked<FMassSmartObjectRequestResultFragment>(RequestEntity);
	return RequestFragment.Result;
}

void FMassSmartObjectHandler::RemoveRequest(const FMassSmartObjectRequestID& RequestID) const
{
	const FMassEntityHandle RequestEntity = static_cast<FMassEntityHandle>(RequestID);
	ExecutionContext.Defer().DestroyEntity(RequestEntity);
}

EMassSmartObjectClaimResult FMassSmartObjectHandler::ClaimCandidate(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FMassSmartObjectRequestID& RequestID) const
{
	if (!ensureMsgf(RequestID.IsSet(), TEXT("Trying to claim using an invalid request.")))
	{
		return EMassSmartObjectClaimResult::Failed_InvalidRequest;
	}

	const FMassEntityHandle RequestEntity = static_cast<FMassEntityHandle>(RequestID);
	const FMassSmartObjectRequestResultFragment& RequestFragment = EntitySubsystem.GetFragmentDataChecked<FMassSmartObjectRequestResultFragment>(RequestEntity);
	if (!ensureMsgf(RequestFragment.Result.bProcessed, TEXT("Trying to claim using a request that is not processed.")))
	{
		return EMassSmartObjectClaimResult::Failed_UnprocessedRequest;
	}

	return ClaimCandidate(Entity, User, RequestFragment.Result);
}

EMassSmartObjectClaimResult FMassSmartObjectHandler::ClaimCandidate(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FMassSmartObjectRequestResult& SearchRequestResult) const
{
	if (!ensureMsgf(SearchRequestResult.bProcessed, TEXT("Trying to claim using a search request that is not processed.")))
	{
		return EMassSmartObjectClaimResult::Failed_UnprocessedRequest;
	}

	checkf(!User.ClaimHandle.IsValid(), TEXT("User should not already have a valid handle."));
	const TConstArrayView<FSmartObjectCandidate> View = MakeArrayView(SearchRequestResult.Candidates.GetData(), SearchRequestResult.NumCandidates);

	for (const FSmartObjectCandidate& Candidate : View)
	{
		if (ClaimSmartObject(Entity, User, Candidate.Handle))
		{
#if WITH_MASSGAMEPLAY_DEBUG
			UE_CVLOG(UE::Mass::Debug::IsDebuggingEntity(Entity),
				&SmartObjectSubsystem,
				LogSmartObject,
				Log,
				TEXT("[%s] claimed [%s]"),
				*Entity.DebugGetDescription(),
				*LexToString(Candidate.Handle));
#endif // WITH_MASSGAMEPLAY_DEBUG
			break;
		}
	}

	return User.ClaimHandle.IsValid() ? EMassSmartObjectClaimResult::Succeeded : EMassSmartObjectClaimResult::Failed_NoAvailableCandidate;
}

bool FMassSmartObjectHandler::ClaimSmartObject(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const FSmartObjectHandle& ObjectHandle) const
{
	FSmartObjectRequestFilter Filter;
	Filter.BehaviorDefinitionClass = USmartObjectMassBehaviorDefinition::StaticClass();

	const FSmartObjectClaimHandle ClaimHandle = SmartObjectSubsystem.Claim(ObjectHandle, Filter);
	const bool bSuccess = ClaimHandle.IsValid();
	if (bSuccess)
	{
		User.ClaimHandle = ClaimHandle;
		User.InteractionStatus = EMassSmartObjectInteractionStatus::Unset;
		const FTransform Transform =SmartObjectSubsystem.GetSlotTransform(User.ClaimHandle).Get(FTransform::Identity);
		User.TargetLocation = Transform.GetLocation();
		User.TargetDirection = Transform.GetRotation().Vector();

		// Register callback to abort interaction if slot gets invalidated.
		// Callback will be unregistered by UMassSmartObjectUserFragmentDeinitializer
		UE::Mass::SmartObject::FPayload Payload;
		Payload.Entity = Entity;
		Payload.EntitySubsystem = &EntitySubsystem;
		Payload.SmartObjectSubsystem = &SmartObjectSubsystem;
		Payload.SignalSubsystem = &SignalSubsystem;
		SmartObjectSubsystem.RegisterSlotInvalidationCallback(ClaimHandle, FOnSlotInvalidated::CreateStatic(&UE::Mass::SmartObject::OnSlotInvalidated, Payload));
	}

#if WITH_MASSGAMEPLAY_DEBUG
	UE_CVLOG(UE::Mass::Debug::IsDebuggingEntity(Entity),
		&SmartObjectSubsystem,
		LogSmartObject,
		Log,
		TEXT("[%s] claim for [%s] %s"),
		*Entity.DebugGetDescription(),
		*LexToString(ObjectHandle),
		bSuccess ? TEXT("Succeeded") : TEXT("Failed"));
#endif // WITH_MASSGAMEPLAY_DEBUG

	return bSuccess;
}

bool FMassSmartObjectHandler::UseSmartObject(
	const FMassEntityHandle Entity,
	FMassSmartObjectUserFragment& User,
	const FTransformFragment& Transform) const
{
#if WITH_MASSGAMEPLAY_DEBUG
	UE_CVLOG(UE::Mass::Debug::IsDebuggingEntity(Entity),
		&SmartObjectSubsystem,
		LogSmartObject,
		Log,
		TEXT("[%s] starts using [%s]"),
		*Entity.DebugGetDescription(),
		*LexToString(User.ClaimHandle));
#endif // WITH_MASSGAMEPLAY_DEBUG

	const USmartObjectMassBehaviorDefinition* BehaviorDefinition = SmartObjectSubsystem.Use<USmartObjectMassBehaviorDefinition>(User.ClaimHandle);
	if (BehaviorDefinition == nullptr)
	{
		return false;
	}

	const FMassEntityView EntityView(EntitySubsystem, Entity);
	const FMassBehaviorEntityContext Context(EntityView, SmartObjectSubsystem);
	BehaviorDefinition->Activate(ExecutionContext.Defer(), Context);

	User.InteractionStatus = EMassSmartObjectInteractionStatus::InProgress;

	return true;
}

void FMassSmartObjectHandler::ReleaseSmartObject(const FMassEntityHandle Entity, FMassSmartObjectUserFragment& User, const EMassSmartObjectInteractionStatus NewStatus) const
{
#if WITH_MASSGAMEPLAY_DEBUG
	UE_CVLOG(UE::Mass::Debug::IsDebuggingEntity(Entity),
		&SmartObjectSubsystem,
		LogSmartObject,
		Log,
		TEXT("[%s] releases handle [%s]"),
		*Entity.DebugGetDescription(),
		*LexToString(User.ClaimHandle));
#endif // WITH_MASSGAMEPLAY_DEBUG

#if DO_ENSURE
	const EMassSmartObjectInteractionStatus CurrentStatus = User.InteractionStatus;

	switch (CurrentStatus)
	{
	case EMassSmartObjectInteractionStatus::Unset:
		ensureMsgf(NewStatus == EMassSmartObjectInteractionStatus::Unset,
			TEXT("Not expecting new status for inactive interaction. Received %s"),
			*UEnum::GetValueAsString(NewStatus));
		break;

	case EMassSmartObjectInteractionStatus::InProgress:
		ensureMsgf(NewStatus == EMassSmartObjectInteractionStatus::BehaviorCompleted
			|| NewStatus == EMassSmartObjectInteractionStatus::TaskCompleted
			|| NewStatus == EMassSmartObjectInteractionStatus::Aborted,
			TEXT("Expecting status 'BehaviorCompleted', 'TaskCompleted' or 'Aborted' for in progress interaction. Received %s"),
			*UEnum::GetValueAsString(NewStatus));
		break;

	case EMassSmartObjectInteractionStatus::BehaviorCompleted:
		ensureMsgf(NewStatus == EMassSmartObjectInteractionStatus::TaskCompleted || NewStatus == EMassSmartObjectInteractionStatus::Aborted,
			TEXT("Expecting status 'TaskCompleted' or 'Aborted' for 'BehaviorCompleted' interaction. Received %s"),
			*UEnum::GetValueAsString(NewStatus));
		break;

	case EMassSmartObjectInteractionStatus::TaskCompleted:
	case EMassSmartObjectInteractionStatus::Aborted:
		ensureMsgf(false, TEXT("Not expecting status changes for'Completed' or 'Aborted' interaction. Current %s - Received %s"),
			*UEnum::GetValueAsString(CurrentStatus),
			*UEnum::GetValueAsString(NewStatus));
		break;

	default:
		ensureMsgf(false, TEXT("Unhandled state: %s"), *UEnum::GetValueAsString(NewStatus));
	}
#endif

	const FMassEntityView EntityView(EntitySubsystem, Entity);
	const FMassBehaviorEntityContext Context(EntityView, SmartObjectSubsystem);
	UE::Mass::SmartObject::Release(ExecutionContext.Defer(), Context, NewStatus);
}
