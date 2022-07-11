// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"

#if WITH_MASSENTITY_DEBUG
#include "Containers/ContainersFwd.h"
#include "VisualLogger/VisualLoggerTypes.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"

class UMassProcessor;
struct FMassEntityQuery;
class UMassEntitySubsystem;
struct FMassArchetypeHandle;
enum class EMassFragmentAccess : uint8;
enum class EMassFragmentPresence : uint8;

MASSENTITY_API DECLARE_ENUM_TO_STRING(EMassFragmentAccess);
MASSENTITY_API DECLARE_ENUM_TO_STRING(EMassFragmentPresence);

namespace UE::Mass::Debug
{
struct MASSENTITY_API FQueryRequirementsView
{
	TConstArrayView<FMassFragmentRequirement> FragmentRequirements;
	TConstArrayView<FMassFragmentRequirement> ChunkRequirements;
	TConstArrayView<FMassFragmentRequirement> ConstSharedRequirements;
	TConstArrayView<FMassFragmentRequirement> SharedRequirements;
	const FMassTagBitSet& RequiredAllTags;
	const FMassTagBitSet& RequiredAnyTags;
	const FMassTagBitSet& RequiredNoneTags;
	const FMassExternalSubystemBitSet& RequiredConstSubsystems;
	const FMassExternalSubystemBitSet& RequiredMutableSubsystems;
};
} // namespace UE::Mass::Debug

struct MASSENTITY_API FMassDebugger
{
	static TConstArrayView<FMassEntityQuery*> GetProcessorQueries(const UMassProcessor& Processor);
	/** fetches all queries registered for given Processor. Note that in order to get up to date information
	 *  FMassEntityQuery::CacheArchetypes will be called on each query */
	static TConstArrayView<FMassEntityQuery*> GetUpToDateProcessorQueries(const UMassEntitySubsystem& EntitySubsystem, UMassProcessor& Processor);

	static UE::Mass::Debug::FQueryRequirementsView GetQueryRequirements(const FMassEntityQuery& Query);
	static void GetQueryExecutionRequirements(const FMassEntityQuery& Query, FMassExecutionRequirements& OutExecutionRequirements);
	
	static TArray<FMassArchetypeHandle> GetAllArchetypes(const UMassEntitySubsystem& EntitySubsystem);
	static const FMassArchetypeCompositionDescriptor& GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle);
	static const FMassArchetypeSharedFragmentValues& GetArchetypeSharedFragmentValues(const FMassArchetypeHandle& ArchetypeHandle);

	static void GetArchetypeEntityStats(const FMassArchetypeHandle& ArchetypeHandle, int32& OutEntitiesCount, int32& OutNumEntitiesPerChunk, int32& OutChunksCount);

	static TConstArrayView<UMassCompositeProcessor::FDependencyNode> GetProcessingGraph(const UMassCompositeProcessor& GraphOwner);
};

#endif // WITH_MASSENTITY_DEBUG