// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassNavigationProcessors.h"
#include "MassCommonUtils.h"
#include "MassCommandBuffer.h"
#include "MassCommonFragments.h"
#include "MassNavigationFragments.h"
#include "MassNavigationSubsystem.h"
#include "MassSimulationLOD.h"
#include "MassMovementTypes.h"
#include "MassMovementFragments.h"
#include "MassEntityView.h"
#include "Engine/World.h"


#define UNSAFE_FOR_MT 0
#define MOVEMENT_DEBUGDRAW 0	// Set to 1 to see heading debugdraw

//----------------------------------------------------------------------//
//  UMassOffLODNavigationProcessor
//----------------------------------------------------------------------//

UMassOffLODNavigationProcessor::UMassOffLODNavigationProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteInGroup = UE::Mass::ProcessorGroupNames::Movement;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Avoidance); // @todo: remove this direct dependency
}

void UMassOffLODNavigationProcessor::ConfigureQueries()
{
	EntityQuery_Conditional.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	EntityQuery_Conditional.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery_Conditional.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
	EntityQuery_Conditional.AddChunkRequirement<FMassSimulationVariableTickChunkFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	EntityQuery_Conditional.SetChunkFilter(&FMassSimulationVariableTickChunkFragment::ShouldTickChunkThisFrame);
}

void UMassOffLODNavigationProcessor::Execute(UMassEntitySubsystem& EntitySubsystem,
													FMassExecutionContext& Context)
{
	EntityQuery_Conditional.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
#if WITH_MASSGAMEPLAY_DEBUG
		if (UE::MassMovement::bFreezeMovement)
		{
			return;
		}
#endif // WITH_MASSGAMEPLAY_DEBUG
		const int32 NumEntities = Context.GetNumEntities();

		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
		const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetFragmentView<FMassMoveTargetFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FTransform& CurrentTransform = LocationList[EntityIndex].GetMutableTransform();
			const FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];

			// Snap position to move target directly
			CurrentTransform.SetLocation(MoveTarget.Center);
		}
	});
}


//----------------------------------------------------------------------//
//  UMassNavigationSmoothHeightProcessor
//----------------------------------------------------------------------//

UMassNavigationSmoothHeightProcessor::UMassNavigationSmoothHeightProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UMassNavigationSmoothHeightProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
	EntityQuery.AddConstSharedRequirement<FMassMovementParameters>(EMassFragmentPresence::All);
}

void UMassNavigationSmoothHeightProcessor::Execute(UMassEntitySubsystem& EntitySubsystem,
													FMassExecutionContext& Context)
{
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
#if WITH_MASSGAMEPLAY_DEBUG
		if (UE::MassMovement::bFreezeMovement)
		{
			return;
		}
#endif // WITH_MASSGAMEPLAY_DEBUG
		const int32 NumEntities = Context.GetNumEntities();
		const float DeltaTime = Context.GetDeltaTimeSeconds();

		const FMassMovementParameters& MovementParams = Context.GetConstSharedFragment<FMassMovementParameters>();
		const TArrayView<FDataFragment_Transform> LocationList = Context.GetMutableFragmentView<FDataFragment_Transform>();
		const TConstArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetFragmentView<FMassMoveTargetFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FTransform& CurrentTransform = LocationList[EntityIndex].GetMutableTransform();
			const FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];

			if (MoveTarget.GetCurrentAction() == EMassMovementAction::Move || MoveTarget.GetCurrentAction() == EMassMovementAction::Stand)
			{
				// Set height smoothly to follow current move targets height.
				FVector CurrentLocation = CurrentTransform.GetLocation();
				FMath::ExponentialSmoothingApprox(CurrentLocation.Z, MoveTarget.Center.Z, DeltaTime, MovementParams.HeightSmoothingTime);
				CurrentTransform.SetLocation(CurrentLocation);
			}
		}
	});
}

//----------------------------------------------------------------------//
//  UMassMoveTargetFragmentInitializer
//----------------------------------------------------------------------//

UMassMoveTargetFragmentInitializer::UMassMoveTargetFragmentInitializer()
{
	FragmentType = FMassMoveTargetFragment::StaticStruct();
}

void UMassMoveTargetFragmentInitializer::ConfigureQueries()
{
	InitializerQuery.AddRequirement<FMassMoveTargetFragment>(EMassFragmentAccess::ReadWrite);
	InitializerQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
}

void UMassMoveTargetFragmentInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	InitializerQuery.ForEachEntityChunk(EntitySubsystem, Context, [](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassMoveTargetFragment> MoveTargetList = Context.GetMutableFragmentView<FMassMoveTargetFragment>();
		const TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassMoveTargetFragment& MoveTarget = MoveTargetList[EntityIndex];
			const FDataFragment_Transform& Location = LocationList[EntityIndex];

			MoveTarget.Center = Location.GetTransform().GetLocation();
			MoveTarget.Forward = Location.GetTransform().GetRotation().Vector();
			MoveTarget.DistanceToGoal = 0.0f;
			MoveTarget.SlackRadius = 0.0f;
		}
	});
}


//----------------------------------------------------------------------//
//  UMassNavigationObstacleGridProcessor
//----------------------------------------------------------------------//
UMassNavigationObstacleGridProcessor::UMassNavigationObstacleGridProcessor()
{
	ExecutionFlags = (int32)EProcessorExecutionFlags::All;
	ExecutionOrder.ExecuteAfter.Add(UE::Mass::ProcessorGroupNames::Movement);
}

void UMassNavigationObstacleGridProcessor::ConfigureQueries()
{
	AddToGridEntityQuery.AddRequirement<FDataFragment_Transform>(EMassFragmentAccess::ReadOnly);
	AddToGridEntityQuery.AddRequirement<FDataFragment_AgentRadius>(EMassFragmentAccess::ReadOnly);
	AddToGridEntityQuery.AddRequirement<FMassNavigationObstacleGridCellLocationFragment>(EMassFragmentAccess::ReadWrite);
	UpdateGridEntityQuery = AddToGridEntityQuery;
	RemoveFromGridEntityQuery = AddToGridEntityQuery;

	AddToGridEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
	AddToGridEntityQuery.AddTagRequirement<FMassInNavigationObstacleGridTag>(EMassFragmentPresence::None);

	UpdateGridEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
	UpdateGridEntityQuery.AddTagRequirement<FMassInNavigationObstacleGridTag>(EMassFragmentPresence::All);

	RemoveFromGridEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
	RemoveFromGridEntityQuery.AddTagRequirement<FMassInNavigationObstacleGridTag>(EMassFragmentPresence::All);
}

void UMassNavigationObstacleGridProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	NavigationSubsystem = UWorld::GetSubsystem<UMassNavigationSubsystem>(Owner.GetWorld());
}

void UMassNavigationObstacleGridProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (!NavigationSubsystem)
	{
		return;
	}

	// can't be ParallelFor due to MovementSubsystem->GetGridMutable().Move not being thread-safe
	AddToGridEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &EntitySubsystem](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
		TConstArrayView<FDataFragment_AgentRadius> RadiiList = Context.GetFragmentView<FDataFragment_AgentRadius>();
		TArrayView<FMassNavigationObstacleGridCellLocationFragment> NavigationObstacleCellLocationList = Context.GetMutableFragmentView<FMassNavigationObstacleGridCellLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// Add to the grid
			const FVector NewPos = LocationList[EntityIndex].GetTransform().GetLocation();
			const float Radius = RadiiList[EntityIndex].Radius;

			FMassNavigationObstacleItem ObstacleItem;
			ObstacleItem.Entity = Context.GetEntity(EntityIndex);
			FMassEntityView EntityView(EntitySubsystem, ObstacleItem.Entity);
			const FMassAvoidanceColliderFragment* Collider = EntityView.GetFragmentDataPtr<FMassAvoidanceColliderFragment>();
			if (Collider)
			{
				ObstacleItem.ItemFlags |= EMassNavigationObstacleFlags::HasColliderData;
			}
			
			const FBox NewBounds(NewPos - FVector(Radius, Radius, 0.f), NewPos + FVector(Radius, Radius, 0.f));
			NavigationObstacleCellLocationList[EntityIndex].CellLoc = NavigationSubsystem->GetObstacleGridMutable().Add(ObstacleItem, NewBounds);

			Context.Defer().AddTag<FMassInNavigationObstacleGridTag>(ObstacleItem.Entity);
		}
	});

	UpdateGridEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this, &EntitySubsystem](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		TConstArrayView<FDataFragment_Transform> LocationList = Context.GetFragmentView<FDataFragment_Transform>();
		TConstArrayView<FDataFragment_AgentRadius> RadiiList = Context.GetFragmentView<FDataFragment_AgentRadius>();
		TArrayView<FMassNavigationObstacleGridCellLocationFragment> NavigationObstacleCellLocationList = Context.GetMutableFragmentView<FMassNavigationObstacleGridCellLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// Update position in grid
			const FVector NewPos = LocationList[EntityIndex].GetTransform().GetLocation();
			const float Radius = RadiiList[EntityIndex].Radius;
			FMassNavigationObstacleItem ObstacleItem;
			ObstacleItem.Entity = Context.GetEntity(EntityIndex);
			FMassEntityView EntityView(EntitySubsystem, ObstacleItem.Entity);
			const FMassAvoidanceColliderFragment* Collider = EntityView.GetFragmentDataPtr<FMassAvoidanceColliderFragment>();
			if (Collider)
			{
				ObstacleItem.ItemFlags |= EMassNavigationObstacleFlags::HasColliderData;
			}
			const FBox NewBounds(NewPos - FVector(Radius, Radius, 0.f), NewPos + FVector(Radius, Radius, 0.f));
			NavigationObstacleCellLocationList[EntityIndex].CellLoc = NavigationSubsystem->GetObstacleGridMutable().Move(ObstacleItem, NavigationObstacleCellLocationList[EntityIndex].CellLoc, NewBounds);

#if WITH_MASSGAMEPLAY_DEBUG && 0
			const FDebugContext BaseDebugContext(this, LogAvoidance, nullptr, ObstacleItem.Entity);
			if (DebugIsSelected(ObstacleItem.Entity))
			{
				FBox Box = MovementSubsystem->GetGridMutable().CalcCellBounds(AvoidanceObstacleCellLocationList[EntityIndex].CellLoc);
				Box.Max.Z += 200.f;					
				DebugDrawBox(BaseDebugContext, Box, FColor::Yellow);
			}
#endif // WITH_MASSGAMEPLAY_DEBUG
		}
	});

	RemoveFromGridEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		TArrayView<FMassNavigationObstacleGridCellLocationFragment> AvoidanceObstacleCellLocationList = Context.GetMutableFragmentView<FMassNavigationObstacleGridCellLocationFragment>();
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			FMassNavigationObstacleItem ObstacleItem;
			ObstacleItem.Entity = Context.GetEntity(EntityIndex);
			NavigationSubsystem->GetObstacleGridMutable().Remove(ObstacleItem, AvoidanceObstacleCellLocationList[EntityIndex].CellLoc);
			AvoidanceObstacleCellLocationList[EntityIndex].CellLoc = FNavigationObstacleHashGrid2D::FCellLocation();

			Context.Defer().RemoveTag<FMassInNavigationObstacleGridTag>(ObstacleItem.Entity);
		}
	});
}

//----------------------------------------------------------------------//
//  UMassNavigationObstacleRemoverProcessor
//----------------------------------------------------------------------//
UMassNavigationObstacleRemoverProcessor::UMassNavigationObstacleRemoverProcessor()
{
	FragmentType = FMassNavigationObstacleGridCellLocationFragment::StaticStruct();
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);
}

void UMassNavigationObstacleRemoverProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassNavigationObstacleGridCellLocationFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassNavigationObstacleRemoverProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	NavigationSubsystem = UWorld::GetSubsystem<UMassNavigationSubsystem>(Owner.GetWorld());
}

void UMassNavigationObstacleRemoverProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (!NavigationSubsystem)
	{
		return;
	}

	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassNavigationObstacleGridCellLocationFragment> AvoidanceObstacleCellLocationList = Context.GetMutableFragmentView<FMassNavigationObstacleGridCellLocationFragment>();

		for (int32 i = 0; i < NumEntities; ++i)
		{
			FMassNavigationObstacleItem ObstacleItem;
			ObstacleItem.Entity = Context.GetEntity(i);
			NavigationSubsystem->GetObstacleGridMutable().Remove(ObstacleItem, AvoidanceObstacleCellLocationList[i].CellLoc);
		}
	});
}

#undef UNSAFE_FOR_MT
