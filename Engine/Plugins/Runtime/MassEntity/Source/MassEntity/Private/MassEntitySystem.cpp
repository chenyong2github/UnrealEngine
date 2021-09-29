// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassEntitySystem.h"
#include "MassEntityTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Containers/UnrealString.h"
#include "Logging/LogScopedVerbosityOverride.h"

#if WITH_PIPE_DEBUG && WITH_AGGREGATETICKING_DEBUG
namespace UE::Pipe::Debug
{
// @todo rename to `brute.*` when time comes

FAutoConsoleCommandWithWorldArgsAndOutputDevice PrintEntityFragmentsCmd(
	TEXT("pipe.PrintEntityFragments"),
	TEXT("Prints all fragment types and values (uproperties) for the specified Entity index"),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
		[](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
		{
			if (UPipeEntitySubsystem* EntitySystem = UPipeEntitySubsystem::GetCurrent(World))
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
				Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find PipeEntitySubsystem for world %s"), *GetPathNameSafe(World));
			}
		})
);

FAutoConsoleCommandWithWorldArgsAndOutputDevice LogArchetypesCmd(
	TEXT("pipe.LogArchetypes"),
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

			if (UPipeEntitySubsystem* EntitySystem = UPipeEntitySubsystem::GetCurrent(World))
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
				Ar.Logf(ELogVerbosity::Error, TEXT("Failed to find PipeEntitySubsystem for world: %s (%s - %s)"),
					*GetPathNameSafe(World),
					LexToString(World->WorldType),
					*ToString(World->GetNetMode()));
			}
		}
	})
);

// @todo these console commands will be reparented to "massentities" domain once we rename and shuffle the modules around 
FAutoConsoleCommandWithWorld RecacheQueries(
	TEXT("pipe.RecacheQueries"),
	TEXT("Forces EntityQueries to recache their valid archetypes"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* InWorld)
		{
			if (UPipeEntitySubsystem* System = UPipeEntitySubsystem::GetCurrent(InWorld))
			{
				System->DebugForceArchetypeDataVersionBump();
			}
		}
	));

FAutoConsoleCommandWithWorldArgsAndOutputDevice LogFragmentSizes(
	TEXT("pipe.LogFragmentSizes"),
	TEXT("Logs all the fragment types being used along with their sizes."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Params, UWorld* World, FOutputDevice& Ar)
		{
			for (const TWeakObjectPtr<const UScriptStruct>& WeakStruct : FLWComponentBitSet::DebugGetAllStructTypes())
			{
				if (const UScriptStruct* StructType = WeakStruct.Get())
				{
					Ar.Logf(ELogVerbosity::Log, TEXT("%s, size: %d"), *StructType->GetName(), StructType->GetStructureSize());
				}
			}
		})
	);

} // namespace UE::Pipe::Debug
#endif // WITH_PIPE_DEBUG && WITH_AGGREGATETICKING_DEBUG

TFunctionRef<UPipeEntitySubsystem* (UWorld*)> UPipeEntitySubsystem::InstanceGetter = [](UWorld* World)
{ 
	return World ? World->GetSubsystem<UPipeEntitySubsystem>() : nullptr;
};
