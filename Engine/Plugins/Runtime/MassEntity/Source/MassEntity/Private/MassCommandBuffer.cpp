// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassCommandBuffer.h"
#include "MassObserverManager.h"
#include "MassEntityUtils.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "VisualLogger/VisualLogger.h"


CSV_DEFINE_CATEGORY(MassEntities, true);
CSV_DEFINE_CATEGORY(MassEntitiesCounters, true);

namespace UE::FLWCCommand {

#if CSV_PROFILER
bool bEnableDetailedStats = false;

FAutoConsoleVariableRef CVarEnableDetailedCommandStats(TEXT("massentities.EnableCommandDetailedStats"), bEnableDetailedStats,
	TEXT("Set to true create a dedicated stat per type of command."), ECVF_Default);

/** CSV stat names */
static FString DefaultName = TEXT("Command");
static TArray<FName> CommandFNames;

/** CSV custom stat names (ANSI) */
static const int32 MaxNameLength = 64;
typedef ANSICHAR ANSIName[MaxNameLength];
static ANSIName DefaultANSIName = "Command";
static TArray<ANSIName> CommandANSINames;

/**
 * Provides valid names for CSV profiling.
 * @param Entry is the view on the command
 * @param OutName is the name to use for csv custom stats
 * @param OutANSIName is the name to use for csv stats
 */
void GetCommandStatNames(FStructView Entry, FString& OutName, ANSIName*& OutANSIName)
{
	OutANSIName = &DefaultANSIName;
	OutName = DefaultName;
	if (!bEnableDetailedStats ||
		!ensureMsgf(Entry.GetScriptStruct() != nullptr, TEXT("Unable to get stat name for an invalid entry. Using default name.")))
	{
		return;
	}

	const FName CommandFName = Entry.GetScriptStruct()->GetFName();
	OutName = CommandFName.ToString();

	const int32 Index = CommandFNames.Find(CommandFName);
	if (Index == INDEX_NONE)
	{
		CommandFNames.Emplace(CommandFName);
		OutANSIName = &CommandANSINames.AddZeroed_GetRef();
		// Use prefix for easier parsing in reports
		//const FString CounterName = FString::Printf(TEXT("Num%s"), *OutName);
		//FMemory::Memcpy(OutANSIName, StringCast<ANSICHAR>(*CounterName).Get(), FMath::Min(CounterName.Len(), MaxNameLength - 1) * sizeof(ANSICHAR));
		FMemory::Memcpy(OutANSIName, StringCast<ANSICHAR>(*OutName).Get(), FMath::Min(OutName.Len(), MaxNameLength - 1) * sizeof(ANSICHAR));
	}
	else
	{
		OutANSIName = &CommandANSINames[Index];
	}
}

#endif
} // UE::FLWCCommand

//////////////////////////////////////////////////////////////////////
// FMassCommandsObservedTypes

void FMassCommandsObservedTypes::Reset()
{
	FragmentsToAdd.Reset();
	FragmentsToRemove.Reset();
}


//////////////////////////////////////////////////////////////////////
// FMassCommandBuffer

void FMassCommandBuffer::ReplayBufferAgainstSystem(UMassEntitySubsystem* EntitySystem)
{
	check(EntitySystem);

	// short-circuit exit
	if (EntitiesToDestroy.Num() == 0 && PendingCommands.IsEmpty())
	{
		return;
	}

	FMassObserverManager& ObserverManager = EntitySystem->GetObserverManager();

	const FMassFragmentBitSet& ObservedAddFragments = ObserverManager.GetObservedAddFragmentsBitSet();
	const FMassFragmentBitSet& ObservedRemoveFragments = ObserverManager.GetObservedRemoveFragmentsBitSet();

	if (ObservedRemoveFragments.IsEmpty() == false)
	{
		for (auto It : ObservedTypes.GetFragmentsToRemove())
		{
			check(It.Key);
			if (ObservedRemoveFragments.Contains(*It.Key))
			{
				TArray<FArchetypeChunkCollection> ChunkCollections;
				UE::Mass::Utils::CreateSparseChunks(*EntitySystem, It.Value, FArchetypeChunkCollection::FoldDuplicates, ChunkCollections);
				for (FArchetypeChunkCollection& Collection : ChunkCollections)
				{
					check(It.Key);
					ObserverManager.OnPreFragmentRemoved(*It.Key, Collection);
				}
			}
		}
	
		TArray<FArchetypeChunkCollection> EntityChunksToDestroy;
		if (EntitiesToDestroy.Num())
		{
			UE::Mass::Utils::CreateSparseChunks(*EntitySystem, EntitiesToDestroy, FArchetypeChunkCollection::FoldDuplicates, EntityChunksToDestroy);
			for (FArchetypeChunkCollection& Collection : EntityChunksToDestroy)
			{
				EntitySystem->BatchDestroyEntityChunks(Collection);
			}
		}
	}
	else if (EntitiesToDestroy.Num())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("ECS Deferred Commands Destroy Entities");
		EntitySystem->BatchDestroyEntities(MakeArrayView(EntitiesToDestroy));
	}
	EntitiesToDestroy.Reset();


	UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
	PendingCommands.ForEach([EntitySystem](FStructView Entry)
	{
		const FCommandBufferEntryBase* Command = Entry.GetPtr<FCommandBufferEntryBase>();
		checkf(Command, TEXT("Either the entry is null or the command does not derive from FCommandBufferEntryBase"));

#if CSV_PROFILER
		using namespace UE::FLWCCommand;

		// Extract name (default or detailed)
		ANSIName* ANSIName = &DefaultANSIName;
		FString Name = DefaultName;
		GetCommandStatNames(Entry, Name, ANSIName);

		// Push stats
		FScopedCsvStat ScopedCsvStat(*ANSIName, CSV_CATEGORY_INDEX(MassEntities));
		FCsvProfiler::RecordCustomStat(*Name, CSV_CATEGORY_INDEX(MassEntitiesCounters), 1, ECsvCustomStatOp::Accumulate);
#endif // CSV_PROFILER

		Command->Execute(*EntitySystem);
	});

	// Using Clear() instead of Reset(), as otherwise the chunks moved into the PendingCommands in MoveAppend() can accumulate.
	PendingCommands.Clear();

	for (auto It : ObservedTypes.GetFragmentsToAdd())
	{
		check(It.Key);
		if (ObservedAddFragments.Contains(*It.Key))
		{
			TArray<FArchetypeChunkCollection> ChunkCollections;
			UE::Mass::Utils::CreateSparseChunks(*EntitySystem, It.Value, FArchetypeChunkCollection::FoldDuplicates, ChunkCollections);
			for (FArchetypeChunkCollection& Collection : ChunkCollections)
			{
				check(It.Key);
				ObserverManager.OnPostFragmentAdded(*It.Key, Collection);
			}
		}
	}

	ObservedTypes.Reset();
}

void FMassCommandBuffer::MoveAppend(FMassCommandBuffer& Other)
{
	// @todo optimize, there surely a way to do faster then this.
	UE_MT_SCOPED_READ_ACCESS(Other.PendingCommandsDetector);
	if (Other.HasPendingCommands() || Other.EntitiesToDestroy.Num())
	{
		FScopeLock Lock(&AppendingCommandsCS);
		UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
		PendingCommands.Append(MoveTemp(Other.PendingCommands));
		EntitiesToDestroy.Append(MoveTemp(Other.EntitiesToDestroy));
	}
}

//////////////////////////////////////////////////////////////////////
// Command implementations

void FCommandAddFragmentInstance::Execute(UMassEntitySubsystem& System) const
{
	System.AddFragmentInstanceListToEntity(TargetEntity, MakeArrayView(&Struct, 1));
}

void FMassCommandAddFragmentInstanceList::Execute(UMassEntitySubsystem& System) const
{
	System.AddFragmentInstanceListToEntity(TargetEntity, FragmentList);
}

void FCommandSwapTags::Execute(UMassEntitySubsystem& System) const
{
	if (System.IsEntityValid(TargetEntity) == false)
	{
		return;
}

	if (OldTagType && NewTagType)
	{
		System.SwapTagsForEntity(TargetEntity, OldTagType, NewTagType);
	}
	else if (OldTagType)
	{
		System.RemoveTagFromEntity(TargetEntity, OldTagType);
	}
	else if (NewTagType)
	{
		System.AddTagToEntity(TargetEntity, NewTagType);
	}
}

void FBuildEntityFromFragmentInstance::Execute(UMassEntitySubsystem& System) const
{
	System.BuildEntity(TargetEntity, MakeArrayView(&Struct, 1));
}

void FBuildEntityFromFragmentInstances::Execute(UMassEntitySubsystem& System) const
{
	System.BuildEntity(TargetEntity, Instances);
}

void FCommandAddFragment::Execute(UMassEntitySubsystem& System) const
{
	System.AddFragmentToEntity(TargetEntity, StructParam);
}

void FCommandRemoveFragment::Execute(UMassEntitySubsystem& System) const
{
	System.RemoveFragmentFromEntity(TargetEntity, StructParam);
}

void FCommandAddFragmentList::Execute(UMassEntitySubsystem& System) const
{
	System.AddFragmentListToEntity(TargetEntity, FragmentList);
}

void FCommandRemoveFragmentList::Execute(UMassEntitySubsystem& System) const
{
	System.RemoveFragmentListFromEntity(TargetEntity, FragmentList);
}

void FCommandAddTag::Execute(UMassEntitySubsystem & System) const 
{
	if (System.IsEntityValid(TargetEntity) == false)
	{
		return;
	}

	System.AddTagToEntity(TargetEntity, StructParam);
}

void FCommandRemoveTag::Execute(UMassEntitySubsystem& System) const
{
	System.RemoveTagFromEntity(TargetEntity, StructParam);
}

void FCommandRemoveComposition::Execute(UMassEntitySubsystem& System) const
{
	if (System.IsEntityValid(TargetEntity) == false)
	{
		return;
	}

	System.RemoveCompositionFromEntity(TargetEntity, Descriptor);
}
