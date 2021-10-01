// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityQuery.h"
#include "MassEntityDebug.h"
#include "MassEntitySubsystem.h"
#include "MassArchetypeData.h"
#include "MassCommandBuffer.h"
#include "VisualLogger/VisualLogger.h"
#include "Async/ParallelFor.h"

//////////////////////////////////////////////////////////////////////
// FMassEntityQuery


FMassEntityQuery::FMassEntityQuery()
{
	ReadCommandlineParams();
}

FMassEntityQuery::FMassEntityQuery(std::initializer_list<UScriptStruct*> InitList)
	: FMassEntityQuery()
{
	for (const UScriptStruct* ComponentType : InitList)
	{
		AddRequirement(ComponentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
}

FMassEntityQuery::FMassEntityQuery(TConstArrayView<const UScriptStruct*> InitList)
	: FMassEntityQuery()
{
	for (const UScriptStruct* ComponentType : InitList)
	{
		AddRequirement(ComponentType, EMassFragmentAccess::ReadWrite, EMassFragmentPresence::All);
	}
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
	// we're sorting the Requirements the same way ArchetypeData's ComponentConfig is sorted (see FMassArchetypeData::Initialize)
	// so that when we access ArchetypeData.ComponentConfigs in FMassArchetypeData::BindRequirementsWithMapping
	// (via GetComponentData call) the access is sequential (i.e. not random) and there's a higher chance the memory
	// ComponentConfigs we want to access have already been fetched and are available in processor cache.
	Requirements.Sort(FMassSorterOperator<FMassFragmentRequirement>());
	ChunkRequirements.Sort(FMassSorterOperator<FMassFragmentRequirement>());
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

			TRACE_CPUPROFILER_EVENT_SCOPE_STR("Pipe RequirementsBinding")
			const TConstArrayView<FMassFragmentRequirement> LocalRequirements = GetRequirements();
			ArchetypeComponentMapping.Reset(ValidArchetypes.Num());
			ArchetypeComponentMapping.AddDefaulted(ValidArchetypes.Num());
			for (int i = 0; i < ValidArchetypes.Num(); ++i)
			{
				ValidArchetypes[i].DataPtr->GetRequirementsComponentMapping(LocalRequirements, ArchetypeComponentMapping[i].EntityComponents);
				if (ChunkRequirements.Num())
				{
					ValidArchetypes[i].DataPtr->GetRequirementsChunkComponentMapping(ChunkRequirements, ArchetypeComponentMapping[i].ChunkComponents);
				}
			}
		}
		else
		{
			UE_VLOG_UELOG(&InEntitySubsystem, LogAggregateTicking, Error, TEXT("FMassEntityQuery::CacheArchetypes: requirements not valid: %s"), *DebugGetDescription());
		}
	}
}

bool FMassEntityQuery::CheckValidity() const
{
	return RequiredAllComponents.IsEmpty() == false || RequiredAnyComponents.IsEmpty() == false || RequiredOptionalComponents.IsEmpty() == false;
}

bool FMassEntityQuery::DoesArchetypeMatchRequirements(const FArchetypeHandle& ArchetypeHandle) const
{
	check(ArchetypeHandle.IsValid());
	const FMassArchetypeData* Archetype = ArchetypeHandle.DataPtr.Get();
	CA_ASSUME(Archetype);

	for (const FMassFragmentRequirement& Requirement : GetRequirements())
	{
		check(Requirement.StructType);
		if (Requirement.IsOptional())
		{
			// at this stage we don't care if the optional is present
			continue;
		}

		const bool bNeedsComponent = (Requirement.Presence == EMassFragmentPresence::All);
		const bool bHasComponent = Archetype->HasComponentType(Requirement.StructType);
		if (bNeedsComponent != bHasComponent)
		{
			return false;
		}
	}
	return true;
}

void FMassEntityQuery::ForEachEntityChunk(const FArchetypeChunkCollection& Chunks, UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
	// mz@todo I don't like that we're copying data here.
	ExecutionContext.SetChunkCollection(Chunks);
	ForEachEntityChunk(EntitySubsystem, ExecutionContext, ExecuteFunction);
	ExecutionContext.ClearChunkCollection();
}

void FMassEntityQuery::ForEachEntityChunk(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& ExecutionContext, const FMassExecuteFunction& ExecuteFunction)
{
#if WITH_MASSENTITY_DEBUG
	int32 NumEntitiesToProcess = 0;
#endif

	// if there's a chunk collection set by the external code - use that
	if (ExecutionContext.GetChunkCollection().IsSet())
	{
		// verify the archetype matches requirements
		if (DoesArchetypeMatchRequirements(ExecutionContext.GetChunkCollection().GetArchetype()) == false)
		{
			// mz@todo add a unit test for this message
			UE_VLOG_UELOG(&EntitySubsystem, LogAggregateTicking, Error, TEXT("Attempted to execute FMassEntityQuery with an incompatible Archetype"));
			return;
		}
		ExecutionContext.SetRequirements(Requirements, ChunkRequirements);
		ExecutionContext.GetChunkCollection().GetArchetype().DataPtr->ExecuteFunction(ExecutionContext, ExecuteFunction, {}, ExecutionContext.GetChunkCollection());
#if WITH_MASSENTITY_DEBUG
		NumEntitiesToProcess = ExecutionContext.GetChunkCollection().GetArchetype().DataPtr->GetNumEntities();
#endif
	}
	else
	{
		CacheArchetypes(EntitySubsystem);
		// it's important to set requirements after caching archetypes due to that call potentially sorting the requirements and the order is relevant here.
		ExecutionContext.SetRequirements(Requirements, ChunkRequirements);

		for (int i = 0; i < ValidArchetypes.Num(); ++i)
		{
			FArchetypeHandle& Archetype = ValidArchetypes[i];
			check(Archetype.IsValid());
			Archetype.DataPtr->ExecuteFunction(ExecutionContext, ExecuteFunction, ArchetypeComponentMapping[i], ChunkCondition);
			ExecutionContext.ClearComponentViews();
#if WITH_MASSENTITY_DEBUG
			NumEntitiesToProcess += Archetype.DataPtr->GetNumEntities();
#endif
		}
	}

#if WITH_MASSENTITY_DEBUG
	// Not using VLOG to be thread safe
	UE_CLOG(!ExecutionContext.DebugGetExecutionDesc().IsEmpty(), LogAggregateTicking, VeryVerbose,
		TEXT("%s: %d entities sent for processing"), *ExecutionContext.DebugGetExecutionDesc(), NumEntitiesToProcess);
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
		const FArchetypeChunkCollection::FChunkInfo ChunkInfo;
	};
	TArray<FChunkJob> Jobs;

	// if there's a chunk collection set by the external code - use that
	if (ExecutionContext.GetChunkCollection().IsSet())
	{
		// verify the archetype matches requirements
		FArchetypeHandle ArchetypeHandle = ExecutionContext.GetChunkCollection().GetArchetype();
		if (DoesArchetypeMatchRequirements(ArchetypeHandle) == false)
		{
			// mz@todo add a unit test for this message
			UE_VLOG_UELOG(&EntitySubsystem, LogAggregateTicking, Error, TEXT("Attempted to execute FMassEntityQuery with an incompatible Archetype"));
			return;
		}

		ExecutionContext.SetRequirements(Requirements, ChunkRequirements);
		check(ArchetypeHandle.IsValid());
		FMassArchetypeData& ArchetypeRef = *ArchetypeHandle.DataPtr.Get();
		const FArchetypeChunkCollection AsChunkCollection(ArchetypeHandle.DataPtr);
		for (const FArchetypeChunkCollection::FChunkInfo& ChunkInfo : AsChunkCollection.GetChunks())
		{
			Jobs.Add({ ArchetypeRef, INDEX_NONE, ChunkInfo });
		}
	}
	else
	{
		CacheArchetypes(EntitySubsystem);
		ExecutionContext.SetRequirements(Requirements, ChunkRequirements);
		for (int ArchetypeIndex = 0; ArchetypeIndex < ValidArchetypes.Num(); ++ArchetypeIndex)
		{
			FArchetypeHandle& Archetype = ValidArchetypes[ArchetypeIndex];
			check(Archetype.IsValid());
			FMassArchetypeData& ArchetypeRef = *Archetype.DataPtr.Get();
			const FArchetypeChunkCollection AsChunkCollection(Archetype.DataPtr);
			for (const FArchetypeChunkCollection::FChunkInfo& ChunkInfo : AsChunkCollection.GetChunks())
			{
				Jobs.Add({ArchetypeRef, ArchetypeIndex, ChunkInfo});
			}
		}
	}
	// ExecutionContext passed by copy on purpose
	ParallelFor(Jobs.Num(), [this, ExecutionContext, &ExecuteFunction, Jobs](const int32 JobIndex)
	{
		Jobs[JobIndex].Archetype.ExecutionFunctionForChunk(ExecutionContext, ExecuteFunction
			, Jobs[JobIndex].ArchetypeIndex != INDEX_NONE ? ArchetypeComponentMapping[Jobs[JobIndex].ArchetypeIndex] : FMassQueryRequirementIndicesMapping()
			, Jobs[JobIndex].ChunkInfo
			, ChunkCondition);
	});

	ExecutionContext.ClearExecutionData();
	ExecutionContext.FlushDeferred(EntitySubsystem);
}

int32 FMassEntityQuery::GetMatchingEntitiesNum(UMassEntitySubsystem& InEntitySubsystem)
{
	CacheArchetypes(InEntitySubsystem);
	int32 TotalEntities = 0;
	for (FArchetypeHandle& ArchetypeHandle : ValidArchetypes)
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

	for (FArchetypeHandle& ArchetypeHandle : ValidArchetypes)
	{
		const FMassArchetypeData* Archetype = ArchetypeHandle.DataPtr.Get();
		if (Archetype && Archetype->GetNumEntities() > 0)
		{
			return true;
		}
	}
	return false;
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

//////////////////////////////////////////////////////////////////////
// FMassFragmentRequirement

FString FMassFragmentRequirement::DebugGetDescription() const
{
#if WITH_MASSENTITY_DEBUG
	return FString::Printf(TEXT("%s%s[%s]"), IsOptional() ? TEXT("?") : (Presence == EMassFragmentPresence::None ? TEXT("-") : TEXT("+"))
		, *GetNameSafe(StructType), *UE::Mass::Debug::DebugGetComponentAccessString(AccessMode));
#else
	return {};
#endif
}
