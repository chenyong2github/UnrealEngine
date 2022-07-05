// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDebugger.h"
#if WITH_MASSENTITY_DEBUG
#include "MassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassArchetypeTypes.h"
#include "MassArchetypeData.h"


DEFINE_ENUM_TO_STRING(EMassFragmentAccess, "/Script/MassEntity");
DEFINE_ENUM_TO_STRING(EMassFragmentPresence, "/Script/MassEntity");

//----------------------------------------------------------------------//
// FMassDebugger
//----------------------------------------------------------------------//
TConstArrayView<FMassEntityQuery*> FMassDebugger::GetProcessorQueries(const UMassProcessor& Processor)
{
	return Processor.OwnedQueries;
}

UE::Mass::Debug::FQueryRequirementsView FMassDebugger::GetQueryRequirements(const FMassEntityQuery& Query)
{
	UE::Mass::Debug::FQueryRequirementsView View = { Query.Requirements, Query.ChunkRequirements, Query.ConstSharedRequirements, Query.SharedRequirements
		, Query.RequiredAllTags, Query.RequiredAnyTags, Query.RequiredNoneTags
		, Query.RequiredConstSubsystems, Query.RequiredMutableSubsystems };

	return View;
}

void FMassDebugger::GetQueryExecutionRequirements(const FMassEntityQuery& Query, FMassExecutionRequirements& OutExecutionRequirements)
{
	Query.ExportRequirements(OutExecutionRequirements);
}

TArray<FMassArchetypeHandle> FMassDebugger::GetAllArchetypes(const UMassEntitySubsystem& EntitySubsystem)
{
	TArray<FMassArchetypeHandle> Archetypes;

	for (auto& KVP : EntitySubsystem.FragmentHashToArchetypeMap)
	{
		for (const TSharedPtr<FMassArchetypeData>& Archetype : KVP.Value)
		{
			Archetypes.Add(FMassArchetypeHandle(Archetype));
		}
	}

	return Archetypes;
}

const FMassArchetypeCompositionDescriptor& FMassDebugger::GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle)
{
	check(ArchetypeHandle.IsValid());
	return ArchetypeHandle.DataPtr->CompositionDescriptor;
}

const FMassArchetypeSharedFragmentValues& FMassDebugger::GetArchetypeSharedFragmentValues(const FMassArchetypeHandle& ArchetypeHandle)
{
	check(ArchetypeHandle.IsValid());
	return ArchetypeHandle.DataPtr->SharedFragmentValues;
}

void FMassDebugger::GetArchetypeEntityStats(const FMassArchetypeHandle& ArchetypeHandle
	, int32& OutEntitiesCount, int32& OutNumEntitiesPerChunk, int32& OutChunksCount)
{
	check(ArchetypeHandle.IsValid());
	OutEntitiesCount = ArchetypeHandle.DataPtr->GetNumEntities();
	OutNumEntitiesPerChunk = ArchetypeHandle.DataPtr->GetNumEntitiesPerChunk();
	OutChunksCount = ArchetypeHandle.DataPtr->GetChunkCount();
}

TConstArrayView<struct UMassCompositeProcessor::FDependencyNode> FMassDebugger::GetProcessingGraph(const UMassCompositeProcessor& GraphOwner)
{
	return GraphOwner.ProcessingFlatGraph;
}

#endif // WITH_MASSENTITY_DEBUG