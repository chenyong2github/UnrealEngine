// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphAnnotationProcessors.h"
#include "MassAIBehaviorTypes.h"
#include "MassZoneGraphMovementFragments.h"
#include "MassZoneGraphAnnotationFragments.h"
#include "MassZoneGraphAnnotationTypes.h"
#include "ZoneGraphTypes.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "MassSignalSubsystem.h"
#include "MassSimulationLOD.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
//  UMassZoneGraphAnnotationTagsInitializer
//----------------------------------------------------------------------//
UMassZoneGraphAnnotationTagsInitializer::UMassZoneGraphAnnotationTagsInitializer()
{
	FragmentType = FMassZoneGraphAnnotationTagsFragment::StaticStruct();
}

void UMassZoneGraphAnnotationTagsInitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(Owner.GetWorld());
}

void UMassZoneGraphAnnotationTagsInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassZoneGraphAnnotationTagsFragment>(ELWComponentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(ELWComponentAccess::ReadOnly);
}

void UMassZoneGraphAnnotationTagsInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetEntitiesNum();
		const TArrayView<FMassZoneGraphAnnotationTagsFragment> AnnotationTagsList = Context.GetMutableComponentView<FMassZoneGraphAnnotationTagsFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetComponentView<FMassZoneGraphLaneLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphAnnotationTagsFragment& AnnotationTags = AnnotationTagsList[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];

			if (!LaneLocation.LaneHandle.IsValid())
			{
				AnnotationTags.Tags = FZoneGraphTagMask::None;
			}
			else
			{
				AnnotationTags.Tags = ZoneGraphAnnotationSubsystem->GetAnnotationTags(LaneLocation.LaneHandle);
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMassZoneGraphAnnotationTagUpdateProcessor
//----------------------------------------------------------------------//
UMassZoneGraphAnnotationTagUpdateProcessor::UMassZoneGraphAnnotationTagUpdateProcessor()
{
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::UpdateAnnotationTags;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::SyncWorldToMass);
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
}

void UMassZoneGraphAnnotationTagUpdateProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(Owner.GetWorld());

	SubscribeToSignal(UE::Mass::Signals::CurrentLaneChanged);
}

void UMassZoneGraphAnnotationTagUpdateProcessor::ConfigureQueries()
{
	Super::ConfigureQueries();
	EntityQuery.AddRequirement<FMassZoneGraphAnnotationTagsFragment>(ELWComponentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddChunkRequirement<FMassZoneGraphAnnotationVariableTickChunkFragment>(ELWComponentAccess::ReadOnly);
	EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(ELWComponentAccess::ReadOnly);
}

void UMassZoneGraphAnnotationTagUpdateProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context)
{
	TransientEntitiesToSignal.Reset();

	// Calling super will update the signals, and call SignalEntities() below.
	Super::Execute(EntitySubsystem, Context);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		// Periodically update tags.
		if (!FMassZoneGraphAnnotationVariableTickChunkFragment::UpdateChunk(Context))
		{
			return;
		}

		const int32 NumEntities = Context.GetEntitiesNum();
		const TArrayView<FMassZoneGraphAnnotationTagsFragment> AnnotationTagsList = Context.GetMutableComponentView<FMassZoneGraphAnnotationTagsFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetComponentView<FMassZoneGraphLaneLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphAnnotationTagsFragment& AnnotationTags = AnnotationTagsList[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];

			UpdateAnnotationTags(AnnotationTags, LaneLocation, Context.GetEntity(EntityIndex));
		}
	});

	if (TransientEntitiesToSignal.Num())
	{
		SignalSubsystem->SignalEntities(UE::Mass::Signals::AnnotationTagsChanged, TransientEntitiesToSignal);
	}
}

void UMassZoneGraphAnnotationTagUpdateProcessor::UpdateAnnotationTags(FMassZoneGraphAnnotationTagsFragment& AnnotationTags, const FMassZoneGraphLaneLocationFragment& LaneLocation, const FLWEntity Entity)
{
	const FZoneGraphTagMask OldTags = AnnotationTags.Tags;

	if (LaneLocation.LaneHandle.IsValid())
	{
		AnnotationTags.Tags = ZoneGraphAnnotationSubsystem->GetAnnotationTags(LaneLocation.LaneHandle);
	}
	else
	{
		AnnotationTags.Tags = FZoneGraphTagMask::None;
	}

	if (OldTags != AnnotationTags.Tags)
	{
		TransientEntitiesToSignal.Add(Entity);
	}
}

void UMassZoneGraphAnnotationTagUpdateProcessor::SignalEntities(UMassEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	if (!ZoneGraphAnnotationSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FLWComponentSystemExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetEntitiesNum();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetComponentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassZoneGraphAnnotationTagsFragment> AnnotationTagsList = Context.GetMutableComponentView<FMassZoneGraphAnnotationTagsFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphAnnotationTagsFragment& AnnotationTags = AnnotationTagsList[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];

			UpdateAnnotationTags(AnnotationTags, LaneLocation, Context.GetEntity(EntityIndex));
		}
	});
}
