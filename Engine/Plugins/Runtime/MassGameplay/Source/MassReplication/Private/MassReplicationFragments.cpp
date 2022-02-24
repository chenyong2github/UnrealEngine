// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassReplicationFragments.h"
#include "MassReplicationSubsystem.h"
#include "MassReplicationProcessor.h"
#include "Engine/World.h"

//----------------------------------------------------------------------//
// UMassNetworkIDFragmentInitializer 
//----------------------------------------------------------------------//
UMassNetworkIDFragmentInitializer::UMassNetworkIDFragmentInitializer()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	ObservedType = FMassNetworkIDFragment::StaticStruct();
	Operation = EMassObservedOperation::Add;
}

void UMassNetworkIDFragmentInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassNetworkIDFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassNetworkIDFragmentInitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
#if UE_REPLICATION_COMPILE_SERVER_CODE
	ReplicationSubsystem = UWorld::GetSubsystem<UMassReplicationSubsystem>(Owner.GetWorld());
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
}

void UMassNetworkIDFragmentInitializer::Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context)
{
	QUICK_SCOPE_CYCLE_COUNTER(MassProcessor_InitNetworkID_Run);

	const UWorld* World = EntitySubsystem.GetWorld();
	const ENetMode NetMode = World->GetNetMode();

	if (NetMode != NM_Client)
	{
#if UE_REPLICATION_COMPILE_SERVER_CODE
		check(ReplicationSubsystem);

		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
			{
				const TArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetMutableFragmentView<FMassNetworkIDFragment>();
				const int32 NumEntities = Context.GetNumEntities();

				for (int32 Idx = 0; Idx < NumEntities; ++Idx)
				{
					NetworkIDList[Idx].NetID = ReplicationSubsystem->GetNextAvailableMassNetID();
				}
			});
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
	}
}

//----------------------------------------------------------------------//
//  FMassReplicationParameters
//----------------------------------------------------------------------//

FMassReplicationParameters::FMassReplicationParameters()
{
	LODDistance[EMassLOD::High] = 0.f;
	LODDistance[EMassLOD::Medium] = 1000.f;
	LODDistance[EMassLOD::Low] = 2500.f;
	LODDistance[EMassLOD::Off] = 5000.f;

	LODMaxCount[EMassLOD::High] = 1600;
	LODMaxCount[EMassLOD::Medium] = 3200;
	LODMaxCount[EMassLOD::Low] = 48000;
	LODMaxCount[EMassLOD::Off] = 0;

	LODMaxCountPerViewer[EMassLOD::High] = 100;
	LODMaxCountPerViewer[EMassLOD::Medium] = 200;
	LODMaxCountPerViewer[EMassLOD::Low] = 300;
	LODMaxCountPerViewer[EMassLOD::Off] = 0;

	UpdateInterval[EMassLOD::High] = 0.1f;
	UpdateInterval[EMassLOD::Medium] = 0.2f;
	UpdateInterval[EMassLOD::Low] = 0.3f;
	UpdateInterval[EMassLOD::Off] = 0.5f;
}

//----------------------------------------------------------------------//
//  FMassReplicationSharedFragment
//----------------------------------------------------------------------//
FMassReplicationSharedFragment::FMassReplicationSharedFragment(UMassReplicationSubsystem& ReplicationSubsystem, const FMassReplicationParameters& Params)
{
	LODCalculator.Initialize(Params.LODDistance, Params.BufferHysteresisOnDistancePercentage / 100.0f, Params.LODMaxCount, Params.LODMaxCountPerViewer);
	BubbleInfoClassHandle = ReplicationSubsystem.GetBubbleInfoClassHandle(Params.BubbleInfoClass);

	CachedReplicator = Params.ReplicatorClass.GetDefaultObject();
	checkf(CachedReplicator, TEXT("Expecting a valid replicator class"))
}