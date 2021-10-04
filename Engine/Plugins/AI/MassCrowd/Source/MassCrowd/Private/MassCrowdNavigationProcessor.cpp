// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdNavigationProcessor.h"
#include "MassAIBehaviorTypes.h"
#include "MassCrowdSubsystem.h"
#include "MassCrowdFragments.h"
#include "MassCrowdSettings.h"
#include "MassAIMovementFragments.h"
#include "MassZoneGraphLaneObstacleAnnotations.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "MassZoneGraphMovementFragments.h"
#include "Annotations/ZoneGraphDisturbanceAnnotation.h"
#include "VisualLogger/VisualLogger.h"

//----------------------------------------------------------------------//
// UMassCrowdLaneTrackingSignalProcessor
//----------------------------------------------------------------------//
UMassCrowdLaneTrackingSignalProcessor::UMassCrowdLaneTrackingSignalProcessor()
{
	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::Behavior);
}

void UMassCrowdLaneTrackingSignalProcessor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassCrowdLaneTrackingFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassZoneGraphLaneLocationFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassCrowdLaneTrackingSignalProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	
	MassCrowdSubsystem = UWorld::GetSubsystem<UMassCrowdSubsystem>(Owner.GetWorld());
	checkf(MassCrowdSubsystem != nullptr, TEXT("UMassCrowdSubsystem is mandatory when using MassCrowd processor."));

	SubscribeToSignal(UE::Mass::Signals::CurrentLaneChanged);
}

void UMassCrowdLaneTrackingSignalProcessor::SignalEntities(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context, FMassSignalNameLookup& EntitySignals)
{
	if (!MassCrowdSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetComponentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassCrowdLaneTrackingFragment> LaneTrackingList = Context.GetMutableComponentView<FMassCrowdLaneTrackingFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassZoneGraphLaneLocationFragment& LaneLocation = LaneLocationList[EntityIndex];
			FMassCrowdLaneTrackingFragment& LaneTracking = LaneTrackingList[EntityIndex];
			if (LaneTracking.TrackedLaneHandle != LaneLocation.LaneHandle)
			{
				MassCrowdSubsystem->OnEntityLaneChanged(Context.GetEntity(EntityIndex), LaneTracking.TrackedLaneHandle, LaneLocation.LaneHandle);
				LaneTracking.TrackedLaneHandle = LaneLocation.LaneHandle;
			}
		}
	});
}

//----------------------------------------------------------------------//
// UMassCrowdLaneTrackingDestructor
//----------------------------------------------------------------------//
UMassCrowdLaneTrackingDestructor::UMassCrowdLaneTrackingDestructor()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	FragmentType = FMassCrowdLaneTrackingFragment::StaticStruct();
}

void UMassCrowdLaneTrackingDestructor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	MassCrowdSubsystem = UWorld::GetSubsystem<UMassCrowdSubsystem>(Owner.GetWorld());
	checkf(MassCrowdSubsystem != nullptr, TEXT("UMassCrowdSubsystem is mandatory when using MassCrowd processor."));
}

void UMassCrowdLaneTrackingDestructor::ConfigureQueries()
{
	EntityQuery.AddTagRequirement<FTagFragment_MassCrowd>(EMassFragmentPresence::All);
	EntityQuery.AddRequirement<FMassCrowdLaneTrackingFragment>(EMassFragmentAccess::ReadOnly);
}

void UMassCrowdLaneTrackingDestructor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](const FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TConstArrayView<FMassCrowdLaneTrackingFragment> LaneTrackingList = Context.GetComponentView<FMassCrowdLaneTrackingFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassCrowdLaneTrackingFragment& LaneTracking = LaneTrackingList[EntityIndex];
			if (LaneTracking.TrackedLaneHandle.IsValid())
			{
				MassCrowdSubsystem->OnEntityLaneChanged(Context.GetEntity(EntityIndex), LaneTracking.TrackedLaneHandle, FZoneGraphLaneHandle());
			}

		}
	});
}

//----------------------------------------------------------------------//
// UMassCrowdDynamicObstacleProcessor
//----------------------------------------------------------------------//
UMassCrowdDynamicObstacleProcessor::UMassCrowdDynamicObstacleProcessor()
{
	bAutoRegisterWithProcessingPhases = true;

	ExecutionOrder.ExecuteBefore.Add(UE::Mass::ProcessorGroupNames::UpdateAnnotationTags);
}

void UMassCrowdDynamicObstacleProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	
	ZoneGraphSubsystem = UWorld::GetSubsystem<UZoneGraphSubsystem>(Owner.GetWorld());
	ZoneGraphAnnotationSubsystem = UWorld::GetSubsystem<UZoneGraphAnnotationSubsystem>(Owner.GetWorld());
	
	CrowdSettings = GetDefault<UMassCrowdSettings>();
	checkf(CrowdSettings, TEXT("Settings default object is always expected to be valid"));
}

void UMassCrowdDynamicObstacleProcessor::OnStop(FMassDynamicObstacleFragment& OutObstacle, const float Radius)
{
	Super::OnStop(OutObstacle, Radius);

	if (ZoneGraphSubsystem == nullptr || ZoneGraphAnnotationSubsystem == nullptr)
	{
		return;
	}

	ensureMsgf(OutObstacle.LaneObstacleIDs.IsEmpty(), TEXT("Blocked lanes have not been cleared."));

	// Only send the a disturbance event if we are fully blocking a pedestrian lanes. 
	const FZoneGraphTag CrowdTag = CrowdSettings->CrowdTag;
	FZoneGraphTagFilter TagFilter;
	TagFilter.AnyTags.Add(CrowdTag);
	TArray<FZoneGraphLaneSection> OutLaneSections;
	ZoneGraphSubsystem->FindLaneOverlaps(OutObstacle.LastPosition, Radius, TagFilter, OutLaneSections);
	if (!OutLaneSections.IsEmpty())
	{
		// Add an obstacle disturbance.
		FZoneGraphObstacleDisturbanceArea Disturbance;
		Disturbance.Position = OutObstacle.LastPosition;
		constexpr float EffectRadius = 1000.f;
		Disturbance.Radius = EffectRadius;
		Disturbance.ObstacleRadius = Radius;
		Disturbance.ObstacleID = FMassLaneObstacleID::GetNextUniqueID();
		Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Add;
		ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);

		// Keep track of our obstacles.
		OutObstacle.LaneObstacleIDs.Add(Disturbance.ObstacleID);
	}
}

void UMassCrowdDynamicObstacleProcessor::OnMove(FMassDynamicObstacleFragment& OutObstacle)
{
	Super::OnMove(OutObstacle);

	if (ZoneGraphAnnotationSubsystem == nullptr)
	{
		return;
	}

	// Unblock previously blocked lanes.
	for (const FMassLaneObstacleID ID : OutObstacle.LaneObstacleIDs)
	{
		FZoneGraphObstacleDisturbanceArea Disturbance;
		Disturbance.ObstacleID = ID;
		Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Remove;
		ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);
	}

	OutObstacle.LaneObstacleIDs.Reset();
}
