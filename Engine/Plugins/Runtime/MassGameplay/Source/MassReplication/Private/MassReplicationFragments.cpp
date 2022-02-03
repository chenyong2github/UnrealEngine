// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassReplicationFragments.h"
#include "MassReplicationSubsystem.h"
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
