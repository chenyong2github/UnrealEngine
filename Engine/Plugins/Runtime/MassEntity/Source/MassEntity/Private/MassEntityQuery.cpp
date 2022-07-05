// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityQuery.h"
#include "MassEntityDebug.h"
#include "MassEntitySubsystem.h"
#include "MassArchetypeData.h"
#include "MassCommandBuffer.h"
#include "VisualLogger/VisualLogger.h"
#include "Async/ParallelFor.h"
#include "Containers/UnrealString.h"
#include "MassProcessor.h"
#include "MassProcessorDependencySolver.h"
#if WITH_MASSENTITY_DEBUG
#include "MassRequirementAccessDetector.h"
#endif // WITH_MASSENTITY_DEBUG


namespace UE::Mass::Private
{
	template<typename TContainer>
	void ExportRequirements(TConstArrayView<FMassFragmentRequirement> Requirements, TMassExecutionAccess<TContainer>& Out)
	{
		for (const FMassFragmentRequirement& Requirement : Requirements)
		{
			if (Requirement.Presence != EMassFragmentPresence::None)
			{
				check(Requirement.StructType);
				if (Requirement.AccessMode == EMassFragmentAccess::ReadOnly)
				{
					Out.Read.Add(*Requirement.StructType);
				}
				else if (Requirement.AccessMode == EMassFragmentAccess::ReadWrite)
				{
					Out.Write.Add(*Requirement.StructType);
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////
// FMassEntityQuery


FMassEntityQuery::FMassEntityQuery()
{
	bAllowParallelExecution = false;
	bRequiresGameThreadExecution = false;
	bRequiresMutatingWorldAccess = false;

	ReadCommandlineParams();
}

FMassEntityQuery::FMassEntityQuery(std::initializer_list<UScriptStruct*> InitList)
	: FMassEntityQuery()
{
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassEntityQuery::FMassEntityQuery(TConstArrayView<const UScriptStruct*> InitList)
	: FMassEntityQuery()
{
	for (const UScriptStruct* FragmentType : InitList)
	{
		AddRequirement(FragmentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassEntityQuery::FMassEntityQuery(UMassProcessor& Owner)
{
	RegisterWithProcessor(Owner);
}

void FMassEntityQuery::RegisterWithProcessor(UMassProcessor& Owner)
{
	ExpectedContextType = EMassExecutionContextType::Processor;
	Owner.RegisterQuery(*this);
#if WITH_MASSENTITY_DEBUG
	bRegistered = true;
#endif // WITH_MASSENTITY_DEBUG
}

void FMassEntityQuery::ReadCommandlineParams()
{
	int AllowParallelQueries = -1;
	if (FParse::Value(FCommandLine::Get(), TEXT("ParallelMassQueries="), AllowParallelQueries))
	{
		bAllowParallelExecution = (AllowParallelQueries != 0);
	}
}

void FMassEntityQuery::SortRequirements()
{
	// we're sorting the Requirements the same way ArchetypeData's FragmentConfig is sorted (see FMassArchetypeData::Initialize)
	// so that when we access ArchetypeData.FragmentConfigs in FMassArchetypeData::BindRequirementsWithMapping
	// (via GetFragmentData call) the access is sequential (i.e. not random) and there's a higher chance the memory
	// FragmentConfigs we want to access have already been fetched and are available in processor cache.
	Requirements.Sort(FScriptStructSortOperator());
	ChunkRequirements.Sort(FScriptStructSortOperator());
	ConstSharedRequirements.Sort(FScriptStructSortOperator());
	SharedRequirements.Sort(FScriptStructSortOperator());
}

void FMassEntityQuery::CacheArchetypes(UMassEntitySubsystem& InEntitySubsystem)
{
	const uint32 InEntitySubsystemHash = PointerHash(&InEntitySubsystem);
	if (EntitySubsystemHash != InEntitySubsystemHash || InEntitySubsystem.GetArchetypeDataVersion() != ArchetypeDataVersion)
	{
		if (CheckValidity())
		{
			SortRequirements();

			EntitySubsystemHash = InEntitySubsystemHash;
			ValidArchetypes.Reset();
			InEntitySubsystem.GetValidArchetypes(*this, ValidArchetypes);
			ArchetypeDataVersion = InEntitySubsystem.GetArchetypeDataVersion();

			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Mass RequirementsBinding")
			const TConstArrayView<FMassFragmentRequirement> LocalRequirements = GetRequirements();
			ArchetypeFragmentMapping.Reset(ValidArchetypes.Num());
			ArchetypeFragmentMapping.AddDefaulted(ValidArchetypes.Num());
			for (int i = 0; i < ValidArchetypes.Num(); ++i)
			{
				ValidArchetypes[i].DataPtr->GetRequirementsFragmentMapping(LocalRequirements, ArchetypeFragmentMapping[i].EntityFragments);
				if (ChunkRequirements.Num())
				{
					ValidArchetypes[i].DataPtr->GetRequirementsChunkFragmentMapping(ChunkRequirements, ArchetypeFragmentMapping[i].ChunkFragments);
				}
				if (ConstSharedRequirements.Num())
				{
					ValidArchetypes[i].DataPtr->GetRequirementsConstSharedFragmentMapping(ConstSharedRequirements, ArchetypeFragmentMapping[i].ConstSharedFragments);
				}
				if (SharedRequirements.Num())
				{
					ValidArchetypes[i].DataPtr->GetRequirementsSharedFragmentMapping(SharedRequirements, ArchetypeFragmentMapping[i].SharedFragments);
				}
			}
		}
		else
		{
			UE_VLOG_UELOG(&InEntitySubsystem, LogMass, Error, TEXT("FMassEntityQuery::CacheArchetypes: requirements not valid: %s"), *DebugGetDescription());
		}
	}
}

bool FMassEntityQuery::CheckValidity() const
{
	return RequiredAllFragments.IsEmpty() == false || RequiredAnyFragments.IsEmpty() == false || RequiredOptionalFragments.IsEmpty() == false;
}

bool FMassEntityQuery::DoesArchetypeMatchRequirements(const FMassArchetypeHandle& ArchetypeHandle) const
{
	check(ArchetypeHandle.IsValid());
	const FMassArchetypeData* Archetype = ArchetypeHandle.DataPtr.Get();
	CA_ASSUME(Archetype);
	
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = Archetype->GetCompositionDescriptor();

	return ArchetypeComposition.Fragments.HasAll(RequiredAllFragments)
		&& (RequiredAnyFragments.IsEmpty() || ArchetypeComposition.Fragments.HasAny(RequiredAnyFragments))
		&& ArchetypeComposition.Fragments.HasNone(RequiredNoneFragments)
		&& ArchetypeComposition.Tags.HasAll(RequiredAllTags)
		&& (RequiredAnyTags.IsEmpty() || ArchetypeComposition.Tags.HasAny(RequiredAnyTags))
		&& ArchetypeComposition.Tags.HasNone(RequiredNoneTags)
		&& ArchetypeComposition.ChunkFragments.HasAll(RequiredAllChunkFragments)
		&& ArchetypeComposition.ChunkFragments.HasNone(RequiredNoneChunkFragments)
		&& ArchetypeComposition.SharedFragments.HasAll(RequiredAllSharedFragments)
		&& ArchetypeComposition.SharedFragments.HasNone(RequiredNoneSharedFragments);
}

void FMassEntityQuery::ForEachEntityChunk(const FMassArchetypeEntityCollection& Collection, UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	// mz@todo I don't like that we're copying data here.
	ExecutionContext.SetEntityCollection(Collection);
	ForEachEntityChunk(EntitySubsystem, ExecutionContext, ExecuteFunction);
	ExecutionContext.ClearEntityCollection();
}

void FMassEntityQuery::ForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
#if WITH_MASSENTITY_DEBUG
	int32 NumEntitiesToProcess = 0;

	checkf(ExecutionContext.ExecutionType == ExpectedContextType && (ExpectedContextType == EMassExecutionContextType::Local || bRegistered)
		, TEXT("ExecutionContextType mismatch, make sure all the queries run as part of processor execution are registered with some processor with a FMassEntityQuery::RegisterWithProcessor call"));

	EntitySubsystem.GetRequirementAccessDetector().RequireAccess(*this);
#endif

	ExecutionContext.SetSubsystemRequirements(RequiredConstSubsystems, RequiredMutableSubsystems);

	// if there's a chunk collection set by the external code - use that
	if (ExecutionContext.GetEntityCollection().IsSet())
	{
		const FMassArchetypeHandle& ArchetypeHandle = ExecutionContext.GetEntityCollection().GetArchetype();
		// verify the archetype matches requirements
		if (DoesArchetypeMatchRequirements(ArchetypeHandle) == false)
		{
			UE_VLOG_UELOG(&EntitySubsystem, LogMass, Log, TEXT("Attempted to execute FMassEntityQuery with an incompatible Archetype: %s")
				, *DebugGetArchetypeCompatibilityDescription(ArchetypeHandle));

#if WITH_MASSENTITY_DEBUG
			EntitySubsystem.GetRequirementAccessDetector().ReleaseAccess(*this);
#endif // WITH_MASSENTITY_DEBUG
			return;
		}
		ExecutionContext.SetRequirements(Requirements, ChunkRequirements, ConstSharedRequirements, SharedRequirements);
		
		ArchetypeHandle.DataPtr->ExecuteFunction(ExecutionContext, ExecuteFunction
			, GetFragmentMappingForArchetype(ArchetypeHandle)
			, ExecutionContext.GetEntityCollection().GetRanges());
#if WITH_MASSENTITY_DEBUG
		NumEntitiesToProcess = ExecutionContext.GetNumEntities();
#endif
	}
	else
	{
		CacheArchetypes(EntitySubsystem);
		// it's important to set requirements after caching archetypes due to that call potentially sorting the requirements and the order is relevant here.
		ExecutionContext.SetRequirements(Requirements, ChunkRequirements, ConstSharedRequirements, SharedRequirements);

		for (int i = 0; i < ValidArchetypes.Num(); ++i)
		{
			FMassArchetypeHandle& Archetype = ValidArchetypes[i];
			check(Archetype.IsValid());
			Archetype.DataPtr->ExecuteFunction(ExecutionContext, ExecuteFunction, ArchetypeFragmentMapping[i], ArchetypeCondition, ChunkCondition);
			ExecutionContext.ClearFragmentViews();
#if WITH_MASSENTITY_DEBUG
			NumEntitiesToProcess += ExecutionContext.GetNumEntities();
#endif
		}
	}

#if WITH_MASSENTITY_DEBUG
	// Not using VLOG to be thread safe
	UE_CLOG(!ExecutionContext.DebugGetExecutionDesc().IsEmpty(), LogMass, VeryVerbose,
		TEXT("%s: %d entities sent for processing"), *ExecutionContext.DebugGetExecutionDesc(), NumEntitiesToProcess);

	EntitySubsystem.GetRequirementAccessDetector().ReleaseAccess(*this);
#endif

	ExecutionContext.ClearExecutionData();
	ExecutionContext.FlushDeferred(EntitySubsystem);
}

void FMassEntityQuery::ParallelForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	if (bAllowParallelExecution == false)
	{
		ForEachEntityChunk(EntitySubsystem, ExecutionContext, ExecuteFunction);
		return;
	}

	struct FChunkJob
	{
		FMassArchetypeData& Archetype;
		const int32 ArchetypeIndex;
		const FMassArchetypeEntityCollection::FArchetypeEntityRange EntityRange;
	};
	TArray<FChunkJob> Jobs;

	// if there's a chunk collection set by the external code - use that
	if (ExecutionContext.GetEntityCollection().IsSet())
	{
		// verify the archetype matches requirements
		FMassArchetypeHandle ArchetypeHandle = ExecutionContext.GetEntityCollection().GetArchetype();
		if (DoesArchetypeMatchRequirements(ArchetypeHandle) == false)
		{
			UE_VLOG_UELOG(&EntitySubsystem, LogMass, Log, TEXT("Attempted to execute FMassEntityQuery with an incompatible Archetype: %s")
				, *DebugGetArchetypeCompatibilityDescription(ExecutionContext.GetEntityCollection().GetArchetype()));
			return;
		}

		ExecutionContext.SetRequirements(Requirements, ChunkRequirements, ConstSharedRequirements, SharedRequirements);
		check(ArchetypeHandle.IsValid());
		FMassArchetypeData& ArchetypeRef = *ArchetypeHandle.DataPtr.Get();
		const FMassArchetypeEntityCollection AsEntityCollection(ArchetypeHandle.DataPtr);
		for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange : AsEntityCollection.GetRanges())
		{
			Jobs.Add({ ArchetypeRef, INDEX_NONE, EntityRange });
		}
	}
	else
	{
		CacheArchetypes(EntitySubsystem);
		ExecutionContext.SetRequirements(Requirements, ChunkRequirements, ConstSharedRequirements, SharedRequirements);
		for (int ArchetypeIndex = 0; ArchetypeIndex < ValidArchetypes.Num(); ++ArchetypeIndex)
		{
			FMassArchetypeHandle& Archetype = ValidArchetypes[ArchetypeIndex];
			check(Archetype.IsValid());
			FMassArchetypeData& ArchetypeRef = *Archetype.DataPtr.Get();
			const FMassArchetypeEntityCollection AsEntityCollection(Archetype.DataPtr);
			for (const FMassArchetypeEntityCollection::FArchetypeEntityRange& EntityRange : AsEntityCollection.GetRanges())
			{
				Jobs.Add({ArchetypeRef, ArchetypeIndex, EntityRange});
			}
		}
	}
	// ExecutionContext passed by copy on purpose
	ParallelFor(Jobs.Num(), [this, ExecutionContext, &ExecuteFunction, Jobs](const int32 JobIndex)
	{
		Jobs[JobIndex].Archetype.ExecutionFunctionForChunk(ExecutionContext, ExecuteFunction
			, Jobs[JobIndex].ArchetypeIndex != INDEX_NONE ? ArchetypeFragmentMapping[Jobs[JobIndex].ArchetypeIndex] : FMassQueryRequirementIndicesMapping()
			, Jobs[JobIndex].EntityRange
			, ChunkCondition);
	});

	ExecutionContext.ClearExecutionData();
	ExecutionContext.FlushDeferred(EntitySubsystem);
}

int32 FMassEntityQuery::GetNumMatchingEntities(UMassEntitySubsystem& InEntitySubsystem)
{
	CacheArchetypes(InEntitySubsystem);
	int32 TotalEntities = 0;
	for (FMassArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		if (const FMassArchetypeData* Archetype = ArchetypeHandle.DataPtr.Get())
		{
			TotalEntities += Archetype->GetNumEntities();
		}
	}
	return TotalEntities;
}

bool FMassEntityQuery::HasMatchingEntities(UMassEntitySubsystem& InEntitySubsystem)
{
	CacheArchetypes(InEntitySubsystem);

	for (FMassArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		const FMassArchetypeData* Archetype = ArchetypeHandle.DataPtr.Get();
		if (Archetype && Archetype->GetNumEntities() > 0)
		{
			return true;
		}
	}
	return false;
}

const FMassQueryRequirementIndicesMapping& FMassEntityQuery::GetFragmentMappingForArchetype(const FMassArchetypeHandle ArchetypeHandle) const
{
	static const FMassQueryRequirementIndicesMapping FallbackEmptyMapping;
	const int32 ArchetypeIndex = ValidArchetypes.Find(ArchetypeHandle);
	return ArchetypeIndex != INDEX_NONE ? ArchetypeFragmentMapping[ArchetypeIndex] : FallbackEmptyMapping;
}

void FMassEntityQuery::ExportRequirements(FMassExecutionRequirements& OutRequirements) const
{
	OutRequirements.RequiredSubsystems.Read += RequiredConstSubsystems;
	OutRequirements.RequiredSubsystems.Write += RequiredMutableSubsystems;

	using UE::Mass::Private::ExportRequirements;
	ExportRequirements<FMassFragmentBitSet>(Requirements, OutRequirements.Fragments);
	ExportRequirements<FMassChunkFragmentBitSet>(ChunkRequirements, OutRequirements.ChunkFragments);
	ExportRequirements<FMassSharedFragmentBitSet>(ConstSharedRequirements, OutRequirements.SharedFragments);
	ExportRequirements<FMassSharedFragmentBitSet>(SharedRequirements, OutRequirements.SharedFragments);

	OutRequirements.RequiredAllTags = RequiredAllTags;
	OutRequirements.RequiredAnyTags = RequiredAnyTags;
	OutRequirements.RequiredNoneTags = RequiredNoneTags;
}

FString FMassEntityQuery::DebugGetDescription() const
{
#if WITH_MASSENTITY_DEBUG
	TStringBuilder<256> StringBuilder;
	StringBuilder.Append(TEXT("<"));

	bool bNeedsComma = false;
	for (const FMassFragmentRequirement& Requirement : Requirements)
	{
		if (bNeedsComma)
		{
			StringBuilder.Append(TEXT(","));
		}
		StringBuilder.Append(*Requirement.DebugGetDescription());
		bNeedsComma = true;
	}

	StringBuilder.Append(TEXT(">"));
	return StringBuilder.ToString();
#else
	return {};
#endif
}

FString FMassEntityQuery::DebugGetArchetypeCompatibilityDescription(const FMassArchetypeHandle& ArchetypeHandle) const
{
	if (ArchetypeHandle.IsValid() == false)
	{
		return TEXT("Invalid");
	}
	
	const FMassArchetypeData& Archetype = *ArchetypeHandle.DataPtr;
	FStringOutputDevice OutDescription;
#if WITH_MASSENTITY_DEBUG	
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = Archetype.GetCompositionDescriptor();

	if (ArchetypeComposition.Fragments.HasAll(RequiredAllFragments) == false)
	{
		// missing one of the strictly required fragments
		OutDescription += TEXT("\nMissing required fragments: ");
		(RequiredAllFragments - ArchetypeComposition.Fragments).DebugGetStringDesc(OutDescription);
	}

	if (RequiredAnyFragments.IsEmpty() == false && ArchetypeComposition.Fragments.HasAny(RequiredAnyFragments) == false)
	{
		// missing all of the "any" fragments
		OutDescription += TEXT("\nMissing all \'any\' fragments: ");
		RequiredAnyFragments.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.Fragments.HasNone(RequiredNoneFragments) == false)
	{
		// has some of the fragments required absent
		OutDescription += TEXT("\nHas fragments required absent: ");
		RequiredNoneFragments.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.Tags.HasAll(RequiredAllTags) == false)
	{
		// missing one of the strictly required tags
		OutDescription += TEXT("\nMissing required tags: ");
		(RequiredAllTags - ArchetypeComposition.Tags).DebugGetStringDesc(OutDescription);
	}

	if (RequiredAnyTags.IsEmpty() == false && ArchetypeComposition.Tags.HasAny(RequiredAnyTags) == false)
	{
		// missing all of the "any" tags
		OutDescription += TEXT("\nMissing all \'any\' tags: ");
		RequiredAnyTags.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.Tags.HasNone(RequiredNoneTags) == false)
	{
		// has some of the tags required absent
		OutDescription += TEXT("\nHas tags required absent: ");
		RequiredNoneTags.DebugGetStringDesc(OutDescription);
	}
	
	if (ArchetypeComposition.ChunkFragments.HasAll(RequiredAllChunkFragments) == false)
    {
    	// missing one of the strictly required chunk fragments
    	OutDescription += TEXT("\nMissing required chunk fragments: ");
		(RequiredAllChunkFragments - ArchetypeComposition.ChunkFragments).DebugGetStringDesc(OutDescription);
    }

    if (ArchetypeComposition.ChunkFragments.HasNone(RequiredNoneChunkFragments) == false)
	{
		// has some of the chunk fragments required absent
		OutDescription += TEXT("\nHas chunk fragments required absent: ");
		RequiredNoneChunkFragments.DebugGetStringDesc(OutDescription);
	}
#endif // WITH_MASSENTITY_DEBUG
	
	return OutDescription.Len() > 0 ? static_cast<FString>(OutDescription) : TEXT("Match");
}


//////////////////////////////////////////////////////////////////////
// FMassFragmentRequirement

FString FMassFragmentRequirement::DebugGetDescription() const
{
#if WITH_MASSENTITY_DEBUG
	return FString::Printf(TEXT("%s%s[%s]"), IsOptional() ? TEXT("?") : (Presence == EMassFragmentPresence::None ? TEXT("-") : TEXT("+"))
		, *GetNameSafe(StructType), *UE::Mass::Debug::DebugGetFragmentAccessString(AccessMode));
#else
	return {};
#endif
}
