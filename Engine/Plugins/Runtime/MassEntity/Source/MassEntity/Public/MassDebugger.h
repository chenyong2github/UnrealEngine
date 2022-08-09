// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"
#if WITH_MASSENTITY_DEBUG
#include "Containers/ContainersFwd.h"
#include "MassEntityQuery.h"
#include "MassProcessor.h"
#endif // WITH_MASSENTITY_DEBUG

class FOutputDevice;
class UMassProcessor;
struct FMassEntityQuery;
class UMassEntitySubsystem;
struct FMassArchetypeHandle;
struct FMassFragmentRequirements;
struct FMassFragmentRequirementDescription;
enum class EMassFragmentAccess : uint8;
enum class EMassFragmentPresence : uint8;

#if WITH_MASSENTITY_DEBUG
namespace UE::Mass::Debug
{
struct MASSENTITY_API FQueryRequirementsView
{
	TConstArrayView<FMassFragmentRequirementDescription> FragmentRequirements;
	TConstArrayView<FMassFragmentRequirementDescription> ChunkRequirements;
	TConstArrayView<FMassFragmentRequirementDescription> ConstSharedRequirements;
	TConstArrayView<FMassFragmentRequirementDescription> SharedRequirements;
	const FMassTagBitSet& RequiredAllTags;
	const FMassTagBitSet& RequiredAnyTags;
	const FMassTagBitSet& RequiredNoneTags;
	const FMassExternalSubsystemBitSet& RequiredConstSubsystems;
	const FMassExternalSubsystemBitSet& RequiredMutableSubsystems;
};

FString DebugGetFragmentAccessString(EMassFragmentAccess Access);
MASSENTITY_API extern void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar);
} // namespace UE::Mass::Debug

struct MASSENTITY_API FMassDebugger
{
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEntitySelected, const UMassEntitySubsystem&, const FMassEntityHandle);

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
	static const TConstArrayView<FName> GetArchetypeDebugNames(const FMassArchetypeHandle& ArchetypeHandle);

	static TConstArrayView<UMassCompositeProcessor::FDependencyNode> GetProcessingGraph(const UMassCompositeProcessor& GraphOwner);
	
	static FString GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement);
	static FString GetRequirementsDescription(const FMassFragmentRequirements& Requirements);
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle);

	static void OutputArchetypeDescription(FOutputDevice& Ar, const FMassArchetypeHandle& Archetype);
	static void OutputEntityDescription(FOutputDevice& Ar, const UMassEntitySubsystem& EntitySubsystem, const int32 EntityIndex, const TCHAR* InPrefix = TEXT(""));
	static void OutputEntityDescription(FOutputDevice& Ar, const UMassEntitySubsystem& EntitySubsystem, const FMassEntityHandle Entity, const TCHAR* InPrefix = TEXT(""));

	static void SelectEntity(const UMassEntitySubsystem& EntitySubsystem, const FMassEntityHandle EntityHandle);

	static FOnEntitySelected OnEntitySelectedDelegate;
};

#else

struct MASSENTITY_API FMassDebugger
{
	static FString GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement) { return TEXT("[no debug information]"); }
	static FString GetRequirementsDescription(const FMassFragmentRequirements& Requirements) { return TEXT("[no debug information]"); }
	static FString GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle) { return TEXT("[no debug information]"); }
};

#endif // WITH_MASSENTITY_DEBUG