// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRepresentationProcessor.h"
#include "MassRepresentationSubsystem.h"
#include "MassRepresentationUtils.h"
#include "MassActorSubsystem.h"
#include "MassCommonFragments.h"
#include "MassCommandBuffer.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "Engine/World.h"

namespace UE::MassRepresentation
{
	int32 bAllowKeepActorExtraFrame = 1;
	FAutoConsoleVariableRef CVarAllowKeepActorExtraFrame(TEXT("ai.massrepresentation.AllowKeepActorExtraFrame"), bAllowKeepActorExtraFrame, TEXT("Allow the mass representation to keep actor an extra frame when switching to ISM"), ECVF_Default);
}

//----------------------------------------------------------------------//
// UMassRepresentationProcessor(); 
//----------------------------------------------------------------------//
UMassRepresentationProcessor::UMassRepresentationProcessor()
{
	bAutoRegisterWithProcessingPhases = false;

	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Representation;

	LODRepresentation[EMassLOD::High] = ERepresentationType::HighResSpawnedActor;
	LODRepresentation[EMassLOD::Medium] = ERepresentationType::LowResSpawnedActor;
	LODRepresentation[EMassLOD::Low] = ERepresentationType::StaticMeshInstance;
	LODRepresentation[EMassLOD::Off] = ERepresentationType::None;
}

void UMassRepresentationProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassRepresentationLODFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Actor>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddChunkRequirement<FMassVisualizationChunkFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassRepresentationProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	World = Owner.GetWorld();
	CachedEntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(World);
	RepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(World);

	// Calculate the default representation when actor isn't spawned yet.
	for (int32 LOD = EMassLOD::High; LOD < EMassLOD::Max; LOD++)
	{
		// Find the first representation type after any actors
		if (LODRepresentation[LOD] == ERepresentationType::HighResSpawnedActor ||
			LODRepresentation[LOD] == ERepresentationType::LowResSpawnedActor)
		{
			continue;
		}

		DefaultRepresentationType = LODRepresentation[LOD];
		break;
	}
}

void UMassRepresentationProcessor::UpdateRepresentation(FMassExecutionContext& Context)
{
	check(RepresentationSubsystem);

	const TConstArrayView<FDataFragment_Transform> TransformList = Context.GetFragmentView<FDataFragment_Transform>();
	const TArrayView<FMassRepresentationFragment> RepresentationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();
	const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();
	const TArrayView<FDataFragment_Actor> ActorList = Context.GetMutableFragmentView<FDataFragment_Actor>();

	const bool bDoKeepActorExtraFrame = UE::MassRepresentation::bAllowKeepActorExtraFrame ? bKeepLowResActors : false;

	const int32 NumEntities = Context.GetNumEntities();
	for (int32 EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
	{
		const FMassEntityHandle MassAgent = Context.GetEntity(EntityIdx);
		const FDataFragment_Transform& TransformFragment = TransformList[EntityIdx];
		const FMassRepresentationLODFragment& RepresentationLOD = RepresentationLODList[EntityIdx];
		FMassRepresentationFragment& Representation = RepresentationList[EntityIdx];
		FDataFragment_Actor& ActorInfo = ActorList[EntityIdx];

		// Keeping a copy of the that last calculated previous representation
		const ERepresentationType PrevRepresentationCopy = Representation.PrevRepresentation;
		Representation.PrevRepresentation = Representation.CurrentRepresentation;
		
		ERepresentationType WantedRepresentationType = LODRepresentation[FMath::Min((int32)RepresentationLOD.LOD, (int32)EMassLOD::Off)];

		// Make sure we do not have actor spawned in areas not fully loaded
		if ((WantedRepresentationType == ERepresentationType::HighResSpawnedActor || WantedRepresentationType == ERepresentationType::LowResSpawnedActor) &&
			!RepresentationSubsystem->IsCollisionLoaded(WorldPartitionGridNameContainingCollision, TransformFragment.GetTransform()))
		{
			WantedRepresentationType = DefaultRepresentationType;
		}
		
		auto DisableActorForISM = [&](AActor*& Actor)
		{
			if (!Actor || ActorInfo.IsOwnedByMass())
			{
				// Execute only if the high res is different than the low res Actor 
				// Or if we do not wish to keep the low res actor while in ISM
				if (Representation.HighResTemplateActorIndex != Representation.LowResTemplateActorIndex || !bKeepLowResActors)
				{
					// Try releasing the high actor or any high res spawning request
					if (ReleaseActorOrCancelSpawning(MassAgent, ActorInfo, Representation.HighResTemplateActorIndex, Representation.ActorSpawnRequestHandle, Context.Defer()))
					{
						Actor = ActorInfo.GetOwnedByMassMutable();
					}
					// Do not do the same with low res if indicated so
					if (!bKeepLowResActors && ReleaseActorOrCancelSpawning(MassAgent, ActorInfo, Representation.LowResTemplateActorIndex, Representation.ActorSpawnRequestHandle, Context.Defer()))
					{
						Actor = ActorInfo.GetOwnedByMassMutable();
					}
				}
				// If we already queued spawn request but have changed our mind, continue with it but once we get the actor back, disable it immediately
				if (Representation.ActorSpawnRequestHandle.IsValid())
				{
					Actor = GetOrSpawnActor(MassAgent, ActorInfo, TransformFragment.GetTransform(), Representation.LowResTemplateActorIndex, Representation.ActorSpawnRequestHandle, GetSpawnPriority(RepresentationLOD));
				}
			}
			if (Actor != nullptr)
			{
				SetActorEnabled(EActorEnabledType::Disabled, *Actor, EntityIdx, Context.Defer());
			}
		};

		// Process switch between representation if there is a change in the representation or there is a pending spawning request
		if (WantedRepresentationType != Representation.CurrentRepresentation || Representation.ActorSpawnRequestHandle.IsValid())
		{
			if (Representation.CurrentRepresentation == ERepresentationType::None)
			{
				Representation.PrevTransform = TransformFragment.GetTransform();
				Representation.PrevLODSignificance = RepresentationLOD.LODSignificance;
			}

			AActor* Actor = ActorInfo.GetMutable();
			switch (WantedRepresentationType)
			{
				case ERepresentationType::HighResSpawnedActor:
				case ERepresentationType::LowResSpawnedActor:
				{
					const bool bHighResActor = WantedRepresentationType == ERepresentationType::HighResSpawnedActor;

					// Reuse actor, if it is valid and not owned by mass or same representation as low res without a valid spawning request
					AActor* NewActor = nullptr;
					if (!Actor || ActorInfo.IsOwnedByMass())
					{
						const int16 WantedTemplateActorIndex = bHighResActor ? Representation.HighResTemplateActorIndex : Representation.LowResTemplateActorIndex;

						// If the low res is different than the high res, cancel any pending spawn request that is the opposite of what is needed.
						if (Representation.LowResTemplateActorIndex != Representation.HighResTemplateActorIndex)
						{
							ReleaseActorOrCancelSpawning(MassAgent, ActorInfo, bHighResActor ? Representation.LowResTemplateActorIndex : Representation.HighResTemplateActorIndex, Representation.ActorSpawnRequestHandle, Context.Defer(), /*bCancelSpawningOnly*/true);
							Actor = ActorInfo.GetOwnedByMassMutable();
						}

						// If there isn't any actor yet or
						// If the actor isn't matching the one needed or
						// If there is still a pending spawn request
						// Then try to retrieve/spawn the new actor
						if (!Actor ||
							!RepresentationSubsystem->DoesActorMatchTemplate(*Actor, WantedTemplateActorIndex) ||
							Representation.ActorSpawnRequestHandle.IsValid())
						{
							NewActor = GetOrSpawnActor(MassAgent, ActorInfo, TransformFragment.GetTransform(), WantedTemplateActorIndex, Representation.ActorSpawnRequestHandle, GetSpawnPriority(RepresentationLOD));
						}
						else
						{
							NewActor = Actor;
						}
					}
					else
					{
						NewActor = Actor;
					}

					if (NewActor)
					{
						// Make sure our (re)activated actor is at the simulated position
						// Needs to be done before enabling the actor so the animation initialization can use the new values
						if (Representation.CurrentRepresentation == ERepresentationType::StaticMeshInstance)
						{
							TeleportActor(Representation.PrevTransform, *NewActor, Context.Defer());
						}

						SetActorEnabled(bHighResActor ? EActorEnabledType::HighRes : EActorEnabledType::LowRes, *NewActor, EntityIdx, Context.Defer());
						Representation.CurrentRepresentation = WantedRepresentationType;
					}
					else if (!Actor)
					{
						Representation.CurrentRepresentation = DefaultRepresentationType;
					}
					break;
				}
				case ERepresentationType::StaticMeshInstance:
					if (!bDoKeepActorExtraFrame || 
					   (Representation.PrevRepresentation != ERepresentationType::HighResSpawnedActor && Representation.PrevRepresentation != ERepresentationType::LowResSpawnedActor))
					{
						DisableActorForISM(Actor);
					}
 
					Representation.CurrentRepresentation = ERepresentationType::StaticMeshInstance;
					break;
				case ERepresentationType::None:
					if (!Actor || ActorInfo.IsOwnedByMass())
					{
						// Try releasing both, could have an high res spawned actor and a spawning request for a low res one
						ReleaseActorOrCancelSpawning(MassAgent, ActorInfo, Representation.LowResTemplateActorIndex, Representation.ActorSpawnRequestHandle, Context.Defer());
						ReleaseActorOrCancelSpawning(MassAgent, ActorInfo, Representation.HighResTemplateActorIndex, Representation.ActorSpawnRequestHandle, Context.Defer());
					}
					else
					{
						SetActorEnabled(EActorEnabledType::Disabled, *Actor, EntityIdx, Context.Defer());
					}
					Representation.CurrentRepresentation = ERepresentationType::None;
					break;
				default:
					checkf(false, TEXT("Unsupported LOD type"));
					break;
			}
		}
		else if (bDoKeepActorExtraFrame && 
				 Representation.PrevRepresentation == ERepresentationType::StaticMeshInstance &&
			    (PrevRepresentationCopy == ERepresentationType::HighResSpawnedActor || PrevRepresentationCopy == ERepresentationType::LowResSpawnedActor))
		{
			AActor* Actor = ActorInfo.GetMutable();
			DisableActorForISM(Actor);
		}
	}
}

void UMassRepresentationProcessor::Execute(UMassEntitySubsystem& InEntitySubsystem, FMassExecutionContext& Context)
{
	check(RepresentationSubsystem);

	// Visualize entities
	EntityQuery.ForEachEntityChunk(InEntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		UpdateRepresentation(Context);
	});
}

AActor* UMassRepresentationProcessor::GetOrSpawnActor(const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, const FTransform& Transform, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, const float Priority)
{
	check(RepresentationSubsystem);

	return RepresentationSubsystem->GetOrSpawnActorFromTemplate(MassAgent, Transform, TemplateActorIndex, SpawnRequestHandle, Priority, 
		FMassActorPreSpawnDelegate::CreateUObject(this, &UMassRepresentationProcessor::OnActorPreSpawn), 
		FMassActorPostSpawnDelegate::CreateUObject(this, &UMassRepresentationProcessor::OnActorPostSpawn));
}

bool UMassRepresentationProcessor::ReleaseActorOrCancelSpawning(const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, const int16 TemplateActorIndex, FMassActorSpawnRequestHandle& SpawnRequestHandle, FMassCommandBuffer& CommandBuffer, bool bCancelSpawningOnly /*= false*/)
{
	check(RepresentationSubsystem);
	check(!ActorInfo.IsValid() || ActorInfo.IsOwnedByMass());

	AActor* Actor = ActorInfo.GetOwnedByMassMutable();
	// note that it's fine for Actor to be null. That means the RepresentationSubsystem will try to stop 
	// the spawning of whatever SpawnRequestHandle reference to
	const bool bSuccess = bCancelSpawningOnly ? RepresentationSubsystem->CancelSpawning(MassAgent, TemplateActorIndex, SpawnRequestHandle) :
			RepresentationSubsystem->ReleaseTemplateActorOrCancelSpawning(MassAgent, TemplateActorIndex, Actor, SpawnRequestHandle);
	if (bSuccess)
	{
		Actor = ActorInfo.GetOwnedByMassMutable();
		if (Actor && RepresentationSubsystem->DoesActorMatchTemplate(*Actor, TemplateActorIndex))
		{
			ActorInfo.ResetNoHandleMapUpdate();
			
			TObjectKey<const AActor> ActorKey(Actor); 
			if (UMassActorSubsystem* MassActorSubsystem = UWorld::GetSubsystem<UMassActorSubsystem>(World))
			{
				CommandBuffer.EmplaceCommand<FDeferredCommand>([MassActorSubsystem, ActorKey](UMassEntitySubsystem&)
				{
					MassActorSubsystem->RemoveHandleForActor(ActorKey);
				});
			}
		}
		return true;
	}
	return false;
}

void UMassRepresentationProcessor::SetActorEnabled(const EActorEnabledType EnabledType, AActor& Actor, const int32 EntityIdx, FMassCommandBuffer& CommandBuffer)
{
	const bool bEnabled = EnabledType != EActorEnabledType::Disabled;
	if (Actor.IsActorTickEnabled() != bEnabled)
	{
		Actor.SetActorTickEnabled(bEnabled);
	}
	if (Actor.GetActorEnableCollision() != bEnabled)
	{
		// Deferring this as there is a callback internally that could end up doing things outside of the game thread and will fire checks(Chaos mostly)
		CommandBuffer.EmplaceCommand<FDeferredCommand>( [&Actor,bEnabled](UMassEntitySubsystem&)
		{
			Actor.SetActorEnableCollision(bEnabled);
		});
	}
}

void UMassRepresentationProcessor::TeleportActor(const FTransform& Transform, AActor& Actor, FMassCommandBuffer& CommandBuffer)
{
	if (!Actor.GetTransform().Equals(Transform))
	{
		CommandBuffer.EmplaceCommand<FDeferredCommand>([&Actor, Transform](UMassEntitySubsystem&)
		{
			Actor.SetActorTransform(Transform, /*bSweep*/false, /*OutSweepHitResult*/nullptr, ETeleportType::TeleportPhysics);
		});
	}
}

void UMassRepresentationProcessor::ReleaseAnyActorOrCancelAnySpawning(UMassRepresentationSubsystem& RepresentationSubsystem, const FMassEntityHandle MassAgent)
{
	if (const UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(RepresentationSubsystem.GetWorld()))
	{
		if (FDataFragment_Actor* ActorInfo = EntitySubsystem->GetFragmentDataPtr<FDataFragment_Actor>(MassAgent))
		{
			if (FMassRepresentationFragment* Representation = EntitySubsystem->GetFragmentDataPtr<FMassRepresentationFragment>(MassAgent))
			{
				ReleaseAnyActorOrCancelAnySpawning(RepresentationSubsystem, MassAgent, *ActorInfo, *Representation);
			}
		}
	}
}

void UMassRepresentationProcessor::ReleaseAnyActorOrCancelAnySpawning(UMassRepresentationSubsystem& RepresentationSubsystem, const FMassEntityHandle MassAgent, FDataFragment_Actor& ActorInfo, FMassRepresentationFragment& Representation)
{
	// This method can only release owned by mass actors
	AActor* Actor = ActorInfo.GetOwnedByMassMutable();
	if (Actor)
	{
		// WARNING!
		// Need to reset before ReleaseTemplateActorOrCancelSpawning as this action might move the entity to a new archetype and
		// so the Fragment passed in parameters would not be valid anymore.
		ActorInfo.ResetAndUpdateHandleMap();
	}
	// Try releasing both as we can have a low res actor and a high res spawning request
	RepresentationSubsystem.ReleaseTemplateActorOrCancelSpawning(MassAgent, Representation.HighResTemplateActorIndex, Actor, Representation.ActorSpawnRequestHandle);
	if (Representation.LowResTemplateActorIndex != Representation.HighResTemplateActorIndex)
	{
		RepresentationSubsystem.ReleaseTemplateActorOrCancelSpawning(MassAgent, Representation.LowResTemplateActorIndex, Actor, Representation.ActorSpawnRequestHandle);
	}
}

void UMassRepresentationProcessor::UpdateVisualization(FMassExecutionContext& Context)
{
	FMassVisualizationChunkFragment& ChunkData = UpdateChunkVisibility(Context);
	if (!ChunkData.ShouldUpdateVisualization())
	{
		return;
	}

	UpdateRepresentation(Context);

	// Update entity visibility
	const TArrayView<FMassRepresentationFragment> RepresentationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();
	const TConstArrayView<FMassRepresentationLODFragment> RepresentationLODList = Context.GetFragmentView<FMassRepresentationLODFragment>();

	const int32 NumEntities = Context.GetNumEntities();
	for (int32 EntityIdx = 0; EntityIdx < NumEntities; EntityIdx++)
	{
		const FMassEntityHandle Entity = Context.GetEntity(EntityIdx);
		FMassRepresentationFragment& Representation = RepresentationList[EntityIdx];
		const FMassRepresentationLODFragment& RepresentationLOD = RepresentationLODList[EntityIdx];
		UpdateEntityVisibility(Entity, Representation, RepresentationLOD, ChunkData, Context.Defer());
	}
}

FMassVisualizationChunkFragment& UMassRepresentationProcessor::UpdateChunkVisibility(FMassExecutionContext& Context) const
{
	bool bFirstUpdate = false;

	// Setup chunk fragment data about visibility
	FMassVisualizationChunkFragment& ChunkData = Context.GetMutableChunkFragment<FMassVisualizationChunkFragment>();
	EMassVisibility ChunkVisibility = ChunkData.GetVisibility();
	if (ChunkVisibility == EMassVisibility::Max)
	{
		// The visibility on the chunk fragment data isn't set yet, let see if the Archetype has an visibility tag and set it on the ChunkData
		ChunkVisibility = UE::MassRepresentation::GetVisibilityFromArchetype(Context);
		ChunkData.SetVisibility(ChunkVisibility);
		bFirstUpdate = bSpreadFirstVisualizationUpdate;
	}
	else
	{
		checkfSlow(UE::MassRepresentation::IsVisibilityTagSet(Context, ChunkVisibility), TEXT("Expecting the same Visibility as what we saved in the chunk data, maybe external code is modifying the tags"))
	}

	if (ChunkVisibility == EMassVisibility::CulledByDistance)
	{
		float DeltaTime = ChunkData.GetDeltaTime();
		if (bFirstUpdate)
		{
			// A DeltaTime of 0.0f means it will tick this frame.
			DeltaTime = FMath::RandRange(0.0f, NotVisibleUpdateRate);
		}
		else 
		{
			if (DeltaTime < 0.0f)
			{
				DeltaTime += NotVisibleUpdateRate * (1.0f + FMath::RandRange(-0.1f, 0.1f));
			}
			DeltaTime -= Context.GetDeltaTimeSeconds();
		}

		ChunkData.Update(DeltaTime);
	}

	return ChunkData;
}

void UMassRepresentationProcessor::UpdateEntityVisibility(const FMassEntityHandle Entity, const FMassRepresentationFragment& Representation, const FMassRepresentationLODFragment& RepresentationLOD, FMassVisualizationChunkFragment& ChunkData, FMassCommandBuffer& CommandBuffer)
{
	// Move the visible entities together into same chunks so we can skip entire chunk when not visible as an optimization
	const EMassVisibility Visibility = Representation.CurrentRepresentation != ERepresentationType::None ? 
		EMassVisibility::CanBeSeen : RepresentationLOD.Visibility;
	const EMassVisibility ChunkVisibility = ChunkData.GetVisibility();
	if (ChunkVisibility != Visibility)
	{
		CommandBuffer.PushCommand(FCommandSwapTags(Entity, UE::MassRepresentation::GetTagFromVisibility(ChunkVisibility), UE::MassRepresentation::GetTagFromVisibility(Visibility)));
		ChunkData.SetContainsNewlyVisibleEntity(Visibility == EMassVisibility::CanBeSeen);
	}
}

void UMassRepresentationProcessor::OnActorPreSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle, const FStructView& SpawnRequest)
{
	check(CachedEntitySubsystem);
	check(RepresentationSubsystem);

	const FMassActorSpawnRequest& MassActorSpawnRequest = SpawnRequest.Get<FMassActorSpawnRequest>();
	const FMassEntityView View(*CachedEntitySubsystem, MassActorSpawnRequest.MassAgent);

	if (FDataFragment_Actor* ActorInfo = View.GetFragmentDataPtr<FDataFragment_Actor>())
	{
		// Release any existing actor
		if (AActor* Actor = ActorInfo->GetMutable())
		{
			checkf(ActorInfo->IsOwnedByMass(), TEXT("If we reach here, we expect the actor to be owned by mass, otherwise we should not be spawning a new one one top of this one."));

			// WARNING!
			// Need to reset before ReleaseTemplateActor as this action might move the entity to a new archetype and
			// so the Fragment passed in parameters would not be valid anymore.
			ActorInfo->ResetAndUpdateHandleMap();

			if (const FMassRepresentationFragment* Representation = View.GetFragmentDataPtr<FMassRepresentationFragment>())
			{
				if (!RepresentationSubsystem->ReleaseTemplateActor(MassActorSpawnRequest.MassAgent, Representation->HighResTemplateActorIndex, Actor, /*bImmediate*/ true))
				{
					if (!RepresentationSubsystem->ReleaseTemplateActor(MassActorSpawnRequest.MassAgent, Representation->LowResTemplateActorIndex, Actor, /*bImmediate*/ true))
					{
						checkf(false, TEXT("Expecting to be able to release spawned actor either the high res or low res one"));
					}
				}
			}
		}
	}
}

EMassActorSpawnRequestAction UMassRepresentationProcessor::OnActorPostSpawn(const FMassActorSpawnRequestHandle& SpawnRequestHandle, const FStructView& SpawnRequest)
{
	check(CachedEntitySubsystem);

	const FMassActorSpawnRequest& MassActorSpawnRequest = SpawnRequest.Get<FMassActorSpawnRequest>();
	checkf(MassActorSpawnRequest.SpawnedActor, TEXT("Expecting valid spawned actor"));

	// Might be already done if the actor has a MassAgentComponent via the callback OnMassAgentComponentEntityAssociated on the MassRepresentationSubsystem
	FDataFragment_Actor& ActorInfo = CachedEntitySubsystem->GetFragmentDataChecked<FDataFragment_Actor>(MassActorSpawnRequest.MassAgent);
	if (ActorInfo.IsValid())
	{
		// If already set, make sure it is pointing to the same actor.
		checkf(ActorInfo.Get() == MassActorSpawnRequest.SpawnedActor, TEXT("Expecting the pointer to the spawned actor in the actor fragment"));
	}
	else
	{
		ActorInfo.SetAndUpdateHandleMap(MassActorSpawnRequest.MassAgent, MassActorSpawnRequest.SpawnedActor, true/*bIsOwnedByMass*/);
	}

	PostActorSpawned(MassActorSpawnRequest.MassAgent, *MassActorSpawnRequest.SpawnedActor);

	return EMassActorSpawnRequestAction::Keep;
}

//----------------------------------------------------------------------//
// UMassRepresentationFragmentDestructor 
//----------------------------------------------------------------------//
UMassRepresentationFragmentDestructor::UMassRepresentationFragmentDestructor()
{
	// By putting this to null, this Deinitializer needs to be explicitly added via a Entity traits and will no longer be automatically called
	// This FragmentType should be deprecated and we should always use the traits.
	FragmentType = FMassRepresentationFragment::StaticStruct();
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);
}

void UMassRepresentationFragmentDestructor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	RepresentationSubsystem = UWorld::GetSubsystem<UMassRepresentationSubsystem>(Owner.GetWorld());
	check(RepresentationSubsystem);
}

void UMassRepresentationFragmentDestructor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassRepresentationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FDataFragment_Actor>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddTagRequirement<FMassRepresentationDefaultDestructorTag>(EMassFragmentPresence::All);
}

void UMassRepresentationFragmentDestructor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	check(RepresentationSubsystem);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const TArrayView<FMassRepresentationFragment> RepresentationList = Context.GetMutableFragmentView<FMassRepresentationFragment>();
		const TArrayView<FDataFragment_Actor> ActorList = Context.GetMutableFragmentView<FDataFragment_Actor>();

		const int32 NumEntities = Context.GetNumEntities();
		for (int32 i = 0; i < NumEntities; ++i)
		{
			FMassRepresentationFragment& Representation = RepresentationList[i];
			FDataFragment_Actor& ActorInfo = ActorList[i];

			const FMassEntityHandle MassAgent = Context.GetEntity(i);

			UMassRepresentationProcessor::ReleaseAnyActorOrCancelAnySpawning(*RepresentationSubsystem, MassAgent, ActorInfo, Representation);
		}
	});
}