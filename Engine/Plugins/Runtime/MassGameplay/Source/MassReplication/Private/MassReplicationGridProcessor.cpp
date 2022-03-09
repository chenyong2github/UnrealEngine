// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassReplicationGridProcessor.h"
#include "MassReplicationSubsystem.h"
#include "MassReplicationTypes.h"
#include "MassReplicationFragments.h"
#include "MassCommonFragments.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
//  UMassReplicationGridProcessor
//----------------------------------------------------------------------//
UMassReplicationGridProcessor::UMassReplicationGridProcessor()
{
#if !UE_ALLOW_DEBUG_REPLICATION_BUBBLES_STANDALONE
	ExecutionFlags = int32(EProcessorExecutionFlags::Server);
#else
	ExecutionFlags = int32(EProcessorExecutionFlags::All);
#endif // UE_ALLOW_DEBUG_REPLICATION_BUBBLES_STANDALONE

	ProcessingPhase = EMassProcessingPhase::PostPhysics;
}

void UMassReplicationGridProcessor::ConfigureQueries()
{
	AddToGridEntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	AddToGridEntityQuery.AddRequirement<FAgentRadiusFragment>(EMassFragmentAccess::ReadOnly);
	AddToGridEntityQuery.AddRequirement<FMassReplicationGridCellLocationFragment>(EMassFragmentAccess::ReadWrite);
	UpdateGridEntityQuery = AddToGridEntityQuery;
	RemoveFromGridEntityQuery = AddToGridEntityQuery;

	AddToGridEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
	AddToGridEntityQuery.AddTagRequirement<FMassInReplicationGridTag>(EMassFragmentPresence::None);

	UpdateGridEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::None);
	UpdateGridEntityQuery.AddTagRequirement<FMassInReplicationGridTag>(EMassFragmentPresence::All);

	RemoveFromGridEntityQuery.AddTagRequirement<FMassOffLODTag>(EMassFragmentPresence::All);
	RemoveFromGridEntityQuery.AddTagRequirement<FMassInReplicationGridTag>(EMassFragmentPresence::All);
}

void UMassReplicationGridProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);

	ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(Owner.GetWorld());
}

void UMassReplicationGridProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	check(ReplicationSubsystem);

	// can't be ParallelFor due to ReplicationSubsystem->GetGridMutable().Move not being thread-safe
	FReplicationHashGrid2D& ReplicationGrid = ReplicationSubsystem->GetGridMutable();
	AddToGridEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ReplicationGrid, &EntitySubsystem](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		TConstArrayView<FAgentRadiusFragment> RadiusList = Context.GetFragmentView<FAgentRadiusFragment>();
		TArrayView<FMassReplicationGridCellLocationFragment> ReplicationCellLocationList = Context.GetMutableFragmentView<FMassReplicationGridCellLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// Add to the grid
			const FVector NewPos = LocationList[EntityIndex].GetTransform().GetLocation();
			const float Radius = RadiusList[EntityIndex].Radius;

			const FMassEntityHandle EntityHandle = Context.GetEntity(EntityIndex);
			const FBox NewBounds(NewPos - FVector(Radius, Radius, 0.f), NewPos + FVector(Radius, Radius, 0.f));
			ReplicationCellLocationList[EntityIndex].CellLoc = ReplicationGrid.Add(EntityHandle, NewBounds);

			Context.Defer().AddTag<FMassInReplicationGridTag>(EntityHandle);
		}
	});

	UpdateGridEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ReplicationGrid, &EntitySubsystem](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		TConstArrayView<FTransformFragment> LocationList = Context.GetFragmentView<FTransformFragment>();
		TConstArrayView<FAgentRadiusFragment> RadiusList = Context.GetFragmentView<FAgentRadiusFragment>();
		TArrayView<FMassReplicationGridCellLocationFragment> ReplicationCellLocationList = Context.GetMutableFragmentView<FMassReplicationGridCellLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			// Update position in grid
			const FVector NewPos = LocationList[EntityIndex].GetTransform().GetLocation();
			const float Radius = RadiusList[EntityIndex].Radius;
			const FMassEntityHandle EntityHandle = Context.GetEntity(EntityIndex);
			const FBox NewBounds(NewPos - FVector(Radius, Radius, 0.f), NewPos + FVector(Radius, Radius, 0.f));
			ReplicationCellLocationList[EntityIndex].CellLoc = ReplicationGrid.Move(EntityHandle, ReplicationCellLocationList[EntityIndex].CellLoc, NewBounds);

#if WITH_MASSGAMEPLAY_DEBUG && 0
			const FDebugContext BaseDebugContext(this, LogMassReplication, nullptr, EntityHandle);
			if (DebugIsSelected(EntityHandle))
			{
				FBox Box = ReplicationGrid.CalcCellBounds(ReplicationCellLocationList[EntityIndex].CellLoc);
				Box.Max.Z += 200.f;
				DebugDrawBox(BaseDebugContext, Box, FColor::Yellow);
			}
#endif // WITH_MASSGAMEPLAY_DEBUG
		}
	});

	RemoveFromGridEntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ReplicationGrid](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();

		TArrayView<FMassReplicationGridCellLocationFragment> ReplicationCellLocationList = Context.GetMutableFragmentView<FMassReplicationGridCellLocationFragment>();
		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassEntityHandle EntityHandle = Context.GetEntity(EntityIndex);
			ReplicationGrid.Remove(EntityHandle, ReplicationCellLocationList[EntityIndex].CellLoc);
			ReplicationCellLocationList[EntityIndex].CellLoc = FReplicationHashGrid2D::FCellLocation();

			Context.Defer().RemoveTag<FMassInReplicationGridTag>(EntityHandle);
		}
	});
}

//----------------------------------------------------------------------//
//  UMassReplicationGridRemoverProcessor
//----------------------------------------------------------------------//
UMassReplicationGridRemoverProcessor::UMassReplicationGridRemoverProcessor()
{
	ObservedType = FMassReplicationGridCellLocationFragment::StaticStruct();
	Operation = EMassObservedOperation::Remove;
	ExecutionFlags = (int32)(EProcessorExecutionFlags::All);
}

void UMassReplicationGridRemoverProcessor::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassReplicationGridCellLocationFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassReplicationGridRemoverProcessor::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
	ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(Owner.GetWorld());
}

void UMassReplicationGridRemoverProcessor::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	if (!ReplicationSubsystem)
	{
		return;
	}

	FReplicationHashGrid2D& ReplicationGrid = ReplicationSubsystem->GetGridMutable();
	EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [&ReplicationGrid](FMassExecutionContext& Context)
	{
		const int32 NumEntities = Context.GetNumEntities();
		const TArrayView<FMassReplicationGridCellLocationFragment> ReplicationCellLocationList = Context.GetMutableFragmentView<FMassReplicationGridCellLocationFragment>();

		for (int32 EntityIndex = 0; EntityIndex < NumEntities; ++EntityIndex)
		{
			const FMassEntityHandle EntityHandle = Context.GetEntity(EntityIndex);
			ReplicationGrid.Remove(EntityHandle, ReplicationCellLocationList[EntityIndex].CellLoc);
			ReplicationCellLocationList[EntityIndex].CellLoc = FReplicationHashGrid2D::FCellLocation();
		}
	});
}