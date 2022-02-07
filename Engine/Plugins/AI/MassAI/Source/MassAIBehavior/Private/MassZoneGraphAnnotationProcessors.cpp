// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassZoneGraphAnnotationProcessors.h"
#include "MassAIBehaviorTypes.h"
#include "MassZoneGraphNavigationFragments.h"
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
	ObservedType = FMassZoneGraphAnnotationFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
}

void UMassZoneGraphAnnotationTagsInitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(Owner.GetWorld());
}

void UMassZoneGraphAnnotationTagsInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassZoneGraphAnnotationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassZoneGraphAnnotationTagsInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassZoneGraphAnnotationFragment> AnnotationTagsList = Context.GetMutableFragmentView<FMassZoneGraphAnnotationFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphAnnotationFragment& AnnotationTags = AnnotationTagsList[EntityIndex];
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
	EntityQuery.AddRequirement<FMassZoneGraphAnnotationFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddChunkRequirement<FMassZoneGraphAnnotationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
}

void UMassZoneGraphAnnotationTagUpdateProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	TransientEntitiesToSignal.Reset();

	// Calling super will update the signals, and call SignalEntities() below.
	Super::Execute(EntitySubsystem, Context);

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		// Periodically update tags.
		if (!FMassZoneGraphAnnotationVariableTickChunkFragment::UpdateChunk(Context))
		{
			return;
		}

		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassZoneGraphAnnotationFragment> AnnotationTagsList = Context.GetMutableFragmentView<FMassZoneGraphAnnotationFragment>();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphAnnotationFragment& AnnotationTags = AnnotationTagsList[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];

			UpdateAnnotationTags(AnnotationTags, LaneLocation, Context.GetEntity(EntityIndex));
		}
	});

	if (TransientEntitiesToSignal.Num())
	{
		SignalSubsystem->SignalEntities(UE::Mass::Signals::AnnotationTagsChanged, TransientEntitiesToSignal);
	}
}

void UMassZoneGraphAnnotationTagUpdateProcessor::UpdateAnnotationTags(FMassZoneGraphAnnotationFragment& AnnotationTags, const FMassZoneGraphLaneLocationFragment& LaneLocation, const FMassEntityHandle Entity)
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

void UMassZoneGraphAnnotationTagUpdateProcessor::SignalEntities(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	if (!ZoneGraphAnnotationSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassZoneGraphAnnotationFragment> AnnotationTagsList = Context.GetMutableFragmentView<FMassZoneGraphAnnotationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassZoneGraphAnnotationFragment& AnnotationTags = AnnotationTagsList[EntityIndex];
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];

			UpdateAnnotationTags(AnnotationTags, LaneLocation, Context.GetEntity(EntityIndex));
		}
	});
}
