// Copyright Epic Games, Inc. All Rights Reserved.

#include "LWCCommandBuffer.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"

//////////////////////////////////////////////////////////////////////
// FMassCommandBuffer

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

void FMassCommandBuffer::ReplayBufferAgainstSystem(UMassEntitySubsystem* System)
{
	check(System);

	UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
	PendingCommands.ForEach([System](FStructView Entry)
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

		Command->Execute(*System);
	});

	// Using Clear() instead of Reset(), as otherwise the chunks moved into the PendingCommands in MoveAppend() can accumulate.
	PendingCommands.Clear();

	if (EntitiesToDestroy.Num())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("ECS Deferred Commands Destroy Entities");
		System->BatchDestroyEntities(MakeArrayView(EntitiesToDestroy));
		EntitiesToDestroy.Reset();
	}
}

void FMassCommandBuffer::MoveAppend(FMassCommandBuffer& Other)
{
	// @todo optimize, there surely a way to do faster then this.
	UE_MT_SCOPED_READ_ACCESS(Other.PendingCommandsDetector);
	if (Other.GetPendingCommandsCount() || Other.EntitiesToDestroy.Num())
	{
		FScopeLock Lock(&AppendingCommandsCS);
		UE_MT_SCOPED_WRITE_ACCESS(PendingCommandsDetector);
		PendingCommands.Append(MoveTemp(Other.PendingCommands));
		EntitiesToDestroy.Append(MoveTemp(Other.EntitiesToDestroy));
	}
}

//////////////////////////////////////////////////////////////////////
// Command implementations

void FCommandAddComponentInstance::Execute(UMassEntitySubsystem& System) const
{
	System.AddComponentInstanceListToEntity(TargetEntity, MakeArrayView(&Struct, 1));
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

void FBuildEntityFromComponentInstance::Execute(UMassEntitySubsystem& System) const
{
	System.BuildEntity(TargetEntity, MakeArrayView(&Struct, 1));
}

void FBuildEntityFromComponentInstances::Execute(UMassEntitySubsystem& System) const
{
	System.BuildEntity(TargetEntity, Instances);
}

void FCommandAddComponent::Execute(UMassEntitySubsystem& System) const
{
	System.AddComponentToEntity(TargetEntity, StructParam);
}

void FCommandRemoveComponent::Execute(UMassEntitySubsystem& System) const
{
	System.RemoveComponentFromEntity(TargetEntity, StructParam);
}

void FCommandAddComponentList::Execute(UMassEntitySubsystem& System) const
{
	System.AddComponentListToEntity(TargetEntity, ComponentList);
}

void FCommandRemoveComponentList::Execute(UMassEntitySubsystem& System) const
{
	System.RemoveComponentListFromEntity(TargetEntity, ComponentList);
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
	System.RemoveCompositionFromEntity(TargetEntity, Descriptor);
}
