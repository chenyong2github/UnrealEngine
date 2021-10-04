// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassReplicationTypes.h"
#include "MassReplicationManager.h"
#include "MassSpawnerTypes.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogMassReplication);

//----------------------------------------------------------------------//
// UMassNetworkIDFragmentInitializer 
//----------------------------------------------------------------------//
UMassNetworkIDFragmentInitializer::UMassNetworkIDFragmentInitializer()
{
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Standalone | EProcessorExecutionFlags::Server);
	FragmentType = FMassNetworkIDFragment::StaticStruct();
}

void UMassNetworkIDFragmentInitializer::ConfigureQueries()
{
	EntityQuery.AddRequirement<FMassNetworkIDFragment>(EMassFragmentAccess::ReadWrite);
}

void UMassNetworkIDFragmentInitializer::Initialize(UObject& Owner)
{
	Super::Initialize(Owner);
#if UE_REPLICATION_COMPILE_SERVER_CODE
	ReplicationManager = UWorld::GetSubsystem<UMassReplicationManager>(Owner.GetWorld());
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
		check(ReplicationManager);

		EntityQuery.ForEachEntityChunk(EntitySubsystem, Context, [this](FMassExecutionContext& Context)
			{
				const TArrayView<FMassNetworkIDFragment> NetworkIDList = Context.GetMutableComponentView<FMassNetworkIDFragment>();
				const int32 NumEntities = Context.GetNumEntities();

				for (int32 Idx = 0; Idx < NumEntities; ++Idx)
				{
					NetworkIDList[Idx].NetID = ReplicationManager->GetNextAvailableMassNetID();
				}
			});
#endif //UE_REPLICATION_COMPILE_SERVER_CODE
	}
}
