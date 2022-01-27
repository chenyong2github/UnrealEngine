// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCrowdNavigationProcessor.h"
#include "MassCommonFragments.h"
#include "MassAIBehaviorTypes.h"
#include "MassCrowdSubsystem.h"
#include "MassCrowdFragments.h"
#include "MassCrowdSettings.h"
#include "MassNavigationFragments.h"
#include "ZoneGraphSubsystem.h"
#include "ZoneGraphAnnotationSubsystem.h"
#include "MassZoneGraphNavigationFragments.h"
#include "Annotations/ZoneGraphDisturbanceAnnotation.h"
#include "VisualLogger/VisualLogger.h"
#include "MassSimulationLOD.h"

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
		const TConstArrayView<FMassZoneGraphLaneLocationFragment> LaneLocationList = Context.GetFragmentView<FMassZoneGraphLaneLocationFragment>();
		const TArrayView<FMassCrowdLaneTrackingFragment> LaneTrackingList = Context.GetMutableFragmentView<FMassCrowdLaneTrackingFragment>();

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
		const TConstArrayView<FMassCrowdLaneTrackingFragment> LaneTrackingList = Context.GetFragmentView<FMassCrowdLaneTrackingFragment>();

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

void UMassCrowdDynamicObstacleProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FDataFragment_AgentRadius>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddRequirement<FMassCrowdObstacleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassCrowdDynamicObstacleProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	UWorld* World = EntitySubsystem.GetWorld();

	EntityQuery_Conditional.ForEachEntityChunk(EntitySubsystem, Context, [this, World](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
		const TConstArrayView<FDataFragment_AgentRadius> RadiusList = Context.GetFragmentView<FDataFragment_AgentRadius>();
		const TArrayView<FMassCrowdObstacleFragment> ObstacleDataList = Context.GetMutableFragmentView<FMassCrowdObstacleFragment>();

		const float CurrentTime = World->GetTimeSeconds();

		const FDateTime Now = FDateTime::UtcNow();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// @todo: limit update frequency, this does not need to occur every frame
			const FVector Position = LocationList[EntityIndex].GetTransform().GetLocation();
			const float Radius = RadiusList[EntityIndex].Radius;
			FMassCrowdObstacleFragment& Obstacle = ObstacleDataList[EntityIndex];

			UE_VLOG_LOCATION(this, LogMassNavigationObstacle, Display, Position, Radius, Obstacle.bHasStopped ? FColor::Red : FColor::Green, TEXT(""));

			if ((Position - Obstacle.LastPosition).SquaredLength() < FMath::Square(DistanceBuffer))
			{
				const float TimeElapsed = CurrentTime - Obstacle.LastMovedTimeStamp;
				if (TimeElapsed > DelayBeforeStopNotification && !Obstacle.bHasStopped)
				{
					// The obstacle hasn't moved for a while.
					Obstacle.bHasStopped = true;

					ensureMsgf(Obstacle.LaneObstacleID.IsValid() == false, TEXT("Obstacle should not have been set."));

					Obstacle.LaneObstacleID = FMassLaneObstacleID::GetNextUniqueID();

					constexpr float EffectRadius = 1000.f;

					// Add an obstacle disturbance.
					FZoneGraphObstacleDisturbanceArea Disturbance;
					Disturbance.Position = Obstacle.LastPosition;
					Disturbance.Radius = EffectRadius;
					Disturbance.ObstacleRadius = Radius;
					Disturbance.ObstacleID = Obstacle.LaneObstacleID;
					Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Add;
					ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);
				}
			}
			else
			{
				// Update position and time stamp
				Obstacle.LastPosition = Position;
				Obstacle.LastMovedTimeStamp = CurrentTime;

				// If the obstacle had stopped, signal the move.
				if (Obstacle.bHasStopped)
				{
					Obstacle.bHasStopped = false;

					if (ensureMsgf(Obstacle.LaneObstacleID.IsValid(), TEXT("Obstacle should have been set.")))
					{
						FZoneGraphObstacleDisturbanceArea Disturbance;
						Disturbance.ObstacleID = Obstacle.LaneObstacleID;
						Disturbance.Action = EZoneGraphObstacleDisturbanceAreaAction::Remove;
						ZoneGraphAnnotationSubsystem->SendEvent(Disturbance);

						Obstacle.LaneObstacleID = FMassLaneObstacleID();
					}
				}
			}
		}
	});
}
