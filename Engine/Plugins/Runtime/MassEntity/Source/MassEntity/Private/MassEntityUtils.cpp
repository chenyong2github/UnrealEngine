// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityUtils.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "MassEntitySubsystem.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"

namespace UE::Mass::Utils
{

EProcessorExecutionFlags GetProcessorExecutionFlagsForWold(const UWorld& World)
{
	EProcessorExecutionFlags ExecutionFlags = EProcessorExecutionFlags::None;
	const ENetMode NetMode = World.GetNetMode();
	switch (NetMode)
	{
		case NM_ListenServer:
			ExecutionFlags = EProcessorExecutionFlags::Client | EProcessorExecutionFlags::Server;
			break;
		case NM_DedicatedServer:
			ExecutionFlags = EProcessorExecutionFlags::Server;
			break;
		case NM_Client:
			ExecutionFlags = EProcessorExecutionFlags::Client;
			break;
		default:
			check(NetMode == NM_Standalone);
			ExecutionFlags = EProcessorExecutionFlags::Standalone;
			break;
	}

	return ExecutionFlags;
}

void CreateSparseChunks(const UMassEntitySubsystem& EntitySystem, const TConstArrayView<FMassEntityHandle> Entities
	, const FMassArchetypeSubChunks::EDuplicatesHandling DuplicatesHandling, TArray<FMassArchetypeSubChunks>& OutChunkCollections)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass_CreateSparseChunks");

	TMap<const FMassArchetypeHandle, TArray<FMassEntityHandle>> ArchetypeToEntities;

	for (const FMassEntityHandle& Entity : Entities)
	{
		if (EntitySystem.IsEntityValid(Entity))
		{
			FMassArchetypeHandle Archetype = EntitySystem.GetArchetypeForEntityUnsafe(Entity);
			TArray<FMassEntityHandle>& PerArchetypeEntities = ArchetypeToEntities.FindOrAdd(Archetype);
			PerArchetypeEntities.Add(Entity);
		}
	}

	for (auto& Pair : ArchetypeToEntities)
	{
		OutChunkCollections.Add(FMassArchetypeSubChunks(Pair.Key, Pair.Value, DuplicatesHandling));
	}
}

} // namespace UE::Mass::Utils