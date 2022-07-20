// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassDebugger.h"
#if WITH_MASSENTITY_DEBUG
#include "MassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassArchetypeTypes.h"
#include "MassArchetypeData.h"
#include "MassRequirements.h"
#include "MassEntityQuery.h"
#include "StructTypeBitSet.h"
#include "Misc/OutputDevice.h"
#include "Engine/World.h"
#include "Engine/Engine.h"


namespace UE::Mass::Debug
{

	FString DebugGetFragmentAccessString(EMassFragmentAccess Access)
	{
#if WITH_MASSENTITY_DEBUG
		switch (Access)
		{
		case EMassFragmentAccess::None:	return TEXT("--");
		case EMassFragmentAccess::ReadOnly:	return TEXT("RO");
		case EMassFragmentAccess::ReadWrite:	return TEXT("RW");
		default:
			ensureMsgf(false, TEXT("Missing string conversion for EMassFragmentAccess=%d"), Access);
			break;
		}
#endif // WITH_MASSENTITY_DEBUG
		return TEXT("Missing string conversion");
	}

	void DebugOutputDescription(TConstArrayView<UMassProcessor*> Processors, FOutputDevice& Ar)
	{
#if WITH_MASSENTITY_DEBUG
		const bool bAutoLineEnd = Ar.GetAutoEmitLineTerminator();
		Ar.SetAutoEmitLineTerminator(false);
		for (const UMassProcessor* Proc : Processors)
		{
			if (Proc)
			{
				Proc->DebugOutputDescription(Ar);
				Ar.Logf(TEXT("\n"));
			}
			else
			{
				Ar.Logf(TEXT("NULL\n"));
			}
		}
		Ar.SetAutoEmitLineTerminator(bAutoLineEnd);
#endif // WITH_MASSENTITY_DEBUG
	}

#if WITH_MASSENTITY_DEBUG && WITH_MASSENTITY_DEBUG
	FAutoConsoleCommandWithWorldArgsAndOutputDevice PrintEntityFragmentsCmd(
		TEXT("mass.PrintEntityFragments"),
		TEXT("Prints all fragment types and values (uproperties) for the specified Entity index"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
			[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				check(World);
				if (UMassEntitySubsystem* EntitySystem = World->GetSubsystem<UMassEntitySubsystem>())
				{
					int32 Index = INDEX_NONE;
					if (LexTryParseString<int32>(Index, *Params[0]))
					{
						FMassDebugger::OutputEntityDescription(Ar, *EntitySystem, Index);
					}
					else
					{
						Ar.Logf(ELogVerbosity::Error, TEXT("Entity index parameter must be an integer"));
					}
				}
				else
				{
					Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find MassEntitySubsystem for world %s"), *GetPathNameSafe(World));
				}
			})
	);

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogArchetypesCmd(
		TEXT("mass.LogArchetypes"),
		TEXT("Dumps description of archetypes to log. Optional parameter controls whether to include or exclude non-occupied archetypes. Defaults to 'include'."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld*, FOutputDevice& Ar)
			{
				const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();
				for (const FWorldContext& Context : WorldContexts)
				{
					UWorld* World = Context.World();
					if (World == nullptr || World->IsPreviewWorld())
					{
						continue;
					}

					Ar.Logf(ELogVerbosity::Log, TEXT("Dumping description of archetypes for world: %s (%s - %s)"),
						*GetPathNameSafe(World),
						LexToString(World->WorldType),
						*ToString(World->GetNetMode()));

					if (UMassEntitySubsystem* EntitySystem = World->GetSubsystem<UMassEntitySubsystem>())
					{
						bool bIncludeEmpty = true;
						if (Params.Num())
						{
							LexTryParseString(bIncludeEmpty, *Params[0]);
						}
						Ar.Logf(ELogVerbosity::Log, TEXT("Include empty archetypes: %s"), bIncludeEmpty ? TEXT("TRUE") : TEXT("FALSE"));
						EntitySystem->DebugGetArchetypesStringDetails(Ar, bIncludeEmpty);
					}
					else
					{
						Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find MassEntitySubsystem for world: %s (%s - %s)"),
							*GetPathNameSafe(World),
							LexToString(World->WorldType),
							*ToString(World->GetNetMode()));
					}
				}
			})
	);

	// @todo these console commands will be reparented to "massentities" domain once we rename and shuffle the modules around 
	FAutoConsoleCommandWithWorld RecacheQueries(
		TEXT("mass.RecacheQueries"),
		TEXT("Forces EntityQueries to recache their valid archetypes"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld)
			{
				check(InWorld);
				if (UMassEntitySubsystem* System = InWorld->GetSubsystem<UMassEntitySubsystem>())
				{
					System->DebugForceArchetypeDataVersionBump();
				}
			}
	));

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogFragmentSizes(
		TEXT("mass.LogFragmentSizes"),
		TEXT("Logs all the fragment types being used along with their sizes."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				for (const TWeakObjectPtr<const UScriptStruct>& WeakStruct : FMassFragmentBitSet::DebugGetAllStructTypes())
				{
					if (const UScriptStruct* StructType = WeakStruct.Get())
					{
						Ar.Logf(ELogVerbosity::Log, TEXT("%s, size: %d"), *StructType->GetName(), StructType->GetStructureSize());
					}
				}
			})
	);

	FAutoConsoleCommandWithWorldArgsAndOutputDevice LogMemoryUsage(
		TEXT("mass.LogMemoryUsage"),
		TEXT("Logs how much memory the mass entity system uses"),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
			{
				check(World);
				if (UMassEntitySubsystem* System = World->GetSubsystem<UMassEntitySubsystem>())
				{
					FResourceSizeEx CumulativeResourceSize;
					System->GetResourceSizeEx(CumulativeResourceSize);
					Ar.Logf(ELogVerbosity::Log, TEXT("MassEntity system uses: %d bytes"), CumulativeResourceSize.GetDedicatedSystemMemoryBytes());
				}
			}));

	FAutoConsoleCommandWithOutputDevice LogFragments(
		TEXT("mass.LogKnownFragments"),
		TEXT("Logs all the known tags and fragments along with their \"index\" as stored via bitsets."),
		FConsoleCommandWithOutputDeviceDelegate::CreateStatic([](FOutputDevice& OutputDevice)
			{
				auto PrintKnownTypes = [&OutputDevice](TConstArrayView<TWeakObjectPtr<const UScriptStruct>> AllStructs) {
					int i = 0;
					for (TWeakObjectPtr<const UScriptStruct> Struct : AllStructs)
					{
						if (Struct.IsValid())
						{
							OutputDevice.Logf(TEXT("\t%d. %s"), i++, *Struct->GetName());
						}
					}
				};

				OutputDevice.Logf(TEXT("Known tags:"));
				PrintKnownTypes(FMassTagBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Fragments:"));
				PrintKnownTypes(FMassFragmentBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Shared Fragments:"));
				PrintKnownTypes(FMassSharedFragmentBitSet::DebugGetAllStructTypes());

				OutputDevice.Logf(TEXT("Known Chunk Fragments:"));
				PrintKnownTypes(FMassChunkFragmentBitSet::DebugGetAllStructTypes());
			}));

#endif // WITH_MASSENTITY_DEBUG

} // namespace UE::Mass::Debug

//----------------------------------------------------------------------//
// FMassDebugger
//----------------------------------------------------------------------//
TConstArrayView<FMassEntityQuery*> FMassDebugger::GetProcessorQueries(const UMassProcessor& Processor)
{
	return Processor.OwnedQueries;
}

TConstArrayView<FMassEntityQuery*> FMassDebugger::GetUpToDateProcessorQueries(const UMassEntitySubsystem& EntitySubsystem, UMassProcessor& Processor)
{
	for (FMassEntityQuery* Query : Processor.OwnedQueries)
	{
		if (Query)
		{
			Query->CacheArchetypes(EntitySubsystem);
		}
	}

	return Processor.OwnedQueries;
}

UE::Mass::Debug::FQueryRequirementsView FMassDebugger::GetQueryRequirements(const FMassEntityQuery& Query)
{
	UE::Mass::Debug::FQueryRequirementsView View = { Query.FragmentRequirements, Query.ChunkFragmentRequirements, Query.ConstSharedFragmentRequirements, Query.SharedFragmentRequirements
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
			Archetypes.Add(FMassArchetypeHelper::ArchetypeHandleFromData(Archetype));
		}
	}

	return Archetypes;
}

const FMassArchetypeCompositionDescriptor& FMassDebugger::GetArchetypeComposition(const FMassArchetypeHandle& ArchetypeHandle)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.CompositionDescriptor;
}

const FMassArchetypeSharedFragmentValues& FMassDebugger::GetArchetypeSharedFragmentValues(const FMassArchetypeHandle& ArchetypeHandle)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	return ArchetypeData.SharedFragmentValues;
}

void FMassDebugger::GetArchetypeEntityStats(const FMassArchetypeHandle& ArchetypeHandle
	, int32& OutEntitiesCount, int32& OutNumEntitiesPerChunk, int32& OutChunksCount)
{
	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	OutEntitiesCount = ArchetypeData.GetNumEntities();
	OutNumEntitiesPerChunk = ArchetypeData.GetNumEntitiesPerChunk();
	OutChunksCount = ArchetypeData.GetChunkCount();
}

TConstArrayView<struct UMassCompositeProcessor::FDependencyNode> FMassDebugger::GetProcessingGraph(const UMassCompositeProcessor& GraphOwner)
{
	return GraphOwner.ProcessingFlatGraph;
}

FString FMassDebugger::GetRequirementsDescription(const FMassFragmentRequirements& Requirements)
{
	TStringBuilder<256> StringBuilder;
	StringBuilder.Append(TEXT("<"));

	bool bNeedsComma = false;
	for (const FMassFragmentRequirementDescription& Requirement : Requirements.FragmentRequirements)
	{
		if (bNeedsComma)
		{
			StringBuilder.Append(TEXT(","));
		}
		StringBuilder.Append(*FMassDebugger::GetSingleRequirementDescription(Requirement));
		bNeedsComma = true;
	}

	StringBuilder.Append(TEXT(">"));
	return StringBuilder.ToString();
}

FString FMassDebugger::GetArchetypeRequirementCompatibilityDescription(const FMassFragmentRequirements& Requirements, const FMassArchetypeHandle& ArchetypeHandle)
{
	if (ArchetypeHandle.IsValid() == false)
	{
		return TEXT("Invalid");
	}

	const FMassArchetypeData& ArchetypeData = FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle);
	FStringOutputDevice OutDescription;

	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = ArchetypeData.GetCompositionDescriptor();

	if (ArchetypeComposition.Fragments.HasAll(Requirements.RequiredAllFragments) == false)
	{
		// missing one of the strictly required fragments
		OutDescription += TEXT("\nMissing required fragments: ");
		(Requirements.RequiredAllFragments - ArchetypeComposition.Fragments).DebugGetStringDesc(OutDescription);
	}

	if (Requirements.RequiredAnyFragments.IsEmpty() == false && ArchetypeComposition.Fragments.HasAny(Requirements.RequiredAnyFragments) == false)
	{
		// missing all of the "any" fragments
		OutDescription += TEXT("\nMissing all \'any\' fragments: ");
		Requirements.RequiredAnyFragments.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.Fragments.HasNone(Requirements.RequiredNoneFragments) == false)
	{
		// has some of the fragments required absent
		OutDescription += TEXT("\nHas fragments required absent: ");
		Requirements.RequiredNoneFragments.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.Tags.HasAll(Requirements.RequiredAllTags) == false)
	{
		// missing one of the strictly required tags
		OutDescription += TEXT("\nMissing required tags: ");
		(Requirements.RequiredAllTags - ArchetypeComposition.Tags).DebugGetStringDesc(OutDescription);
	}

	if (Requirements.RequiredAnyTags.IsEmpty() == false && ArchetypeComposition.Tags.HasAny(Requirements.RequiredAnyTags) == false)
	{
		// missing all of the "any" tags
		OutDescription += TEXT("\nMissing all \'any\' tags: ");
		Requirements.RequiredAnyTags.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.Tags.HasNone(Requirements.RequiredNoneTags) == false)
	{
		// has some of the tags required absent
		OutDescription += TEXT("\nHas tags required absent: ");
		Requirements.RequiredNoneTags.DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.ChunkFragments.HasAll(Requirements.RequiredAllChunkFragments) == false)
	{
		// missing one of the strictly required chunk fragments
		OutDescription += TEXT("\nMissing required chunk fragments: ");
		(Requirements.RequiredAllChunkFragments - ArchetypeComposition.ChunkFragments).DebugGetStringDesc(OutDescription);
	}

	if (ArchetypeComposition.ChunkFragments.HasNone(Requirements.RequiredNoneChunkFragments) == false)
	{
		// has some of the chunk fragments required absent
		OutDescription += TEXT("\nHas chunk fragments required absent: ");
		Requirements.RequiredNoneChunkFragments.DebugGetStringDesc(OutDescription);
	}

	return OutDescription.Len() > 0 ? static_cast<FString>(OutDescription) : TEXT("Match");
}

FString FMassDebugger::GetSingleRequirementDescription(const FMassFragmentRequirementDescription& Requirement)
{
	return FString::Printf(TEXT("%s%s[%s]"), Requirement.IsOptional() ? TEXT("?") : (Requirement.Presence == EMassFragmentPresence::None ? TEXT("-") : TEXT("+"))
		, *GetNameSafe(Requirement.StructType), *UE::Mass::Debug::DebugGetFragmentAccessString(Requirement.AccessMode));
}

void FMassDebugger::OutputArchetypeDescription(FOutputDevice& Ar, const FMassArchetypeHandle& ArchetypeHandle)
{
	Ar.Logf(TEXT("%s"), ArchetypeHandle.IsValid() ? *FMassArchetypeHelper::ArchetypeDataFromHandleChecked(ArchetypeHandle).DebugGetDescription() : TEXT("INVALID"));
}

void FMassDebugger::OutputEntityDescription(FOutputDevice& Ar, const UMassEntitySubsystem& EntitySubsystem, const int32 EntityIndex, const TCHAR* InPrefix)
{
	if (EntityIndex >= EntitySubsystem.Entities.Num())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for out of range index in %s"), *GetPathNameSafe(&EntitySubsystem));
		return;
	}

	const UMassEntitySubsystem::FEntityData& EntityData = EntitySubsystem.Entities[EntityIndex];
	if (!EntityData.IsValid())
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in %s"), *GetPathNameSafe(&EntitySubsystem));
	}

	FMassEntityHandle Entity;
	Entity.Index = EntityIndex;
	Entity.SerialNumber = EntityData.SerialNumber;
	OutputEntityDescription(Ar, EntitySubsystem, Entity, InPrefix);
}

void FMassDebugger::OutputEntityDescription(FOutputDevice& Ar, const UMassEntitySubsystem& EntitySubsystem, const FMassEntityHandle Entity, const TCHAR* InPrefix)
{
	if (!EntitySubsystem.IsEntityActive(Entity))
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in %s"), *GetPathNameSafe(&EntitySubsystem));
	}

	Ar.Logf(ELogVerbosity::Log, TEXT("Listing fragments values for Entity[%s] in %s"), *Entity.DebugGetDescription(), *GetPathNameSafe(&EntitySubsystem));

	const UMassEntitySubsystem::FEntityData& EntityData = EntitySubsystem.Entities[Entity.Index];
	FMassArchetypeData* Archetype = EntityData.CurrentArchetype.Get();
	if (Archetype == nullptr)
	{
		Ar.Logf(ELogVerbosity::Log, TEXT("Unable to list fragments values for invalid entity in %s"), *GetPathNameSafe(&EntitySubsystem));
	}
	else
	{
		Archetype->DebugPrintEntity(Entity, Ar, InPrefix);
	}
}

#endif // WITH_MASSENTITY_DEBUG