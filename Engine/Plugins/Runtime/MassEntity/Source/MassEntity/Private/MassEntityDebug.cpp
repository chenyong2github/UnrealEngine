// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntityDebug.h"
#include "Misc/OutputDevice.h"
#include "MassProcessor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

DEFINE_ENUM_TO_STRING(EMassProcessingPhase);

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
					EntitySystem->DebugPrintEntity(Index, Ar);
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


#endif // WITH_MASSENTITY_DEBUG && WITH_MASSENTITY_DEBUG

} // namespace UE::Mass::Debug

