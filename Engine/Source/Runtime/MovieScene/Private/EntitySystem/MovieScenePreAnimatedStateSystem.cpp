// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "IMovieScenePlayer.h"



UMovieSceneCachePreAnimatedStateSystem::UMovieSceneCachePreAnimatedStateSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// This system relies upon anything that creates entities
		DefineComponentConsumer(GetClass(), FBuiltInComponentTypes::Get()->SymbolicTags.CreatesEntities);
	}
}

bool UMovieSceneCachePreAnimatedStateSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	// Always relevant if we're capturing global state
	return InLinker->ShouldCaptureGlobalState() || InLinker->EntityManager.ContainsComponent(FBuiltInComponentTypes::Get()->Tags.RestoreState);
}

void UMovieSceneCachePreAnimatedStateSystem::OnLink()
{
	UMovieSceneRestorePreAnimatedStateSystem* RestoreSystem = Linker->LinkSystem<UMovieSceneRestorePreAnimatedStateSystem>();
	Linker->SystemGraph.AddReference(this, RestoreSystem);
}

void UMovieSceneCachePreAnimatedStateSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	TArray<IMovieScenePreAnimatedStateSystemInterface*, TInlineAllocator<16>> Interfaces;
	auto ForEachSystem = [&Interfaces](UMovieSceneEntitySystem* InSystem)
	{
		if (IMovieScenePreAnimatedStateSystemInterface* PreAnimInterface = Cast<IMovieScenePreAnimatedStateSystemInterface>(InSystem))
		{
			Interfaces.Add(PreAnimInterface);
		}
	};
	Linker->SystemGraph.IteratePhase(ESystemPhase::Spawn, ForEachSystem);
	Linker->SystemGraph.IteratePhase(ESystemPhase::Instantiation, ForEachSystem);

	if (Linker->ShouldCaptureGlobalState())
	{
		for (IMovieScenePreAnimatedStateSystemInterface* Interface : Interfaces)
		{
			Interface->SaveGlobalPreAnimatedState(InPrerequisites, Subsequents);
		}
	}

	for (IMovieScenePreAnimatedStateSystemInterface* Interface : Interfaces)
	{
		Interface->SavePreAnimatedState(InPrerequisites, Subsequents);
	}
}

UMovieSceneRestorePreAnimatedStateSystem::UMovieSceneRestorePreAnimatedStateSystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	using namespace UE::MovieScene;

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		// This system relies upon anything that creates entities
		DefineImplicitPrerequisite(UMovieSceneCachePreAnimatedStateSystem::StaticClass(), GetClass());
	}
}

void UMovieSceneRestorePreAnimatedStateSystem::DiscardPreAnimatedStateForObject(UObject& Object)
{
	using namespace UE::MovieScene;

	TArray<IMovieScenePreAnimatedStateSystemInterface*, TInlineAllocator<16>> Interfaces;

	auto ForEachSystem = [&Interfaces](UMovieSceneEntitySystem* InSystem)
	{
		IMovieScenePreAnimatedStateSystemInterface* PreAnimInterface = Cast<IMovieScenePreAnimatedStateSystemInterface>(InSystem);
		if (PreAnimInterface)
		{
			Interfaces.Add(PreAnimInterface);
		}
	};
	Linker->SystemGraph.IteratePhase(ESystemPhase::Spawn, ForEachSystem);
	Linker->SystemGraph.IteratePhase(ESystemPhase::Instantiation, ForEachSystem);

	for (IMovieScenePreAnimatedStateSystemInterface* Interface : Interfaces)
	{
		Interface->DiscardPreAnimatedStateForObject(Object);
	}
}

void UMovieSceneRestorePreAnimatedStateSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	TArray<IMovieScenePreAnimatedStateSystemInterface*, TInlineAllocator<16>> Interfaces;

	auto ForEachSystem = [&Interfaces](UMovieSceneEntitySystem* InSystem)
	{
		IMovieScenePreAnimatedStateSystemInterface* PreAnimInterface = Cast<IMovieScenePreAnimatedStateSystemInterface>(InSystem);
		if (PreAnimInterface)
		{
			Interfaces.Add(PreAnimInterface);
		}
	};
	Linker->SystemGraph.IteratePhase(ESystemPhase::Spawn, ForEachSystem);
	Linker->SystemGraph.IteratePhase(ESystemPhase::Instantiation, ForEachSystem);

	// Iterate backwards restoring stale state
	for (int32 Index = Interfaces.Num()-1; Index >= 0; --Index)
	{
		Interfaces[Index]->RestorePreAnimatedState(InPrerequisites, Subsequents);
	}
}
