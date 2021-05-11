// Copyright Epic Games, Inc. All Rights Reserved.

#include "EntitySystem/MovieScenePreAnimatedStateSystem.h"
#include "EntitySystem/MovieSceneEntitySystemLinker.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "IMovieScenePlayer.h"

#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateExtension.h"
#include "Evaluation/PreAnimatedState/MovieScenePreAnimatedStateStorage.h"


namespace UE
{
namespace MovieScene
{

TSharedPtr<FPreAnimatedStateExtension> FPreAnimatedStateExtensionReference::Get() const
{
	return WeakPreAnimatedStateExtension.Pin();
}

TSharedPtr<FPreAnimatedStateExtension> FPreAnimatedStateExtensionReference::Update(UMovieSceneEntitySystemLinker* Linker)
{
	FPreAnimatedStateExtension* ExistingExtension = Linker->FindExtension<FPreAnimatedStateExtension>();

	const bool bHasRestoreStateEntities = Linker->EntityManager.ContainsComponent(FBuiltInComponentTypes::Get()->Tags.RestoreState);
	const bool bIsCapturingGlobalState  = ExistingExtension && ExistingExtension->IsCapturingGlobalState();

	if (bIsCapturingGlobalState && WeakPreAnimatedStateExtension.Pin() == nullptr)
	{
		WeakPreAnimatedStateExtension = ExistingExtension->AsShared();
	}

	if (bHasRestoreStateEntities && !PreAnimatedStateExtensionRef)
	{
		if (ExistingExtension)
		{
			PreAnimatedStateExtensionRef = ExistingExtension->AsShared();
			WeakPreAnimatedStateExtension = PreAnimatedStateExtensionRef;
		}
		else
		{
			// FPreAnimatedStateExtension automatically adds itself to the linker
			PreAnimatedStateExtensionRef = MakeShared<FPreAnimatedStateExtension>(Linker);
			WeakPreAnimatedStateExtension = PreAnimatedStateExtensionRef;
		}
	}
	else if (!bHasRestoreStateEntities && PreAnimatedStateExtensionRef)
	{
		PreAnimatedStateExtensionRef = nullptr;
		if (!bIsCapturingGlobalState)
		{
			WeakPreAnimatedStateExtension = nullptr;
		}
	}

	return WeakPreAnimatedStateExtension.Pin();
}


} // namespace MovieScene
} // namespace UE


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
	// This function can be called on the CDO and instances, so care is taken to do the right thing
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return UE::MovieScene::FPreAnimatedStateExtensionReference(InLinker).Get().IsValid();
	}
	return PreAnimatedStateRef.Get().IsValid();
}

void UMovieSceneCachePreAnimatedStateSystem::OnLink()
{
	using namespace UE::MovieScene;

	PreAnimatedStateRef.Update(Linker);
}

void UMovieSceneCachePreAnimatedStateSystem::OnUnlink()
{
	PreAnimatedStateRef = UE::MovieScene::FPreAnimatedStateExtensionReference();
}

void UMovieSceneCachePreAnimatedStateSystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;
	TSharedPtr<UE::MovieScene::FPreAnimatedStateExtension> Extension = PreAnimatedStateRef.Update(Linker);
	if (!Extension)
	{
		return;
	}

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
	Linker->SystemGraph.IteratePhase(ESystemPhase::Evaluation, ForEachSystem);

	IMovieScenePreAnimatedStateSystemInterface::FPreAnimationParameters Params{ &InPrerequisites, &Subsequents, Extension.Get() };
	for (IMovieScenePreAnimatedStateSystemInterface* Interface : Interfaces)
	{
		Interface->SavePreAnimatedState(Params);
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

bool UMovieSceneRestorePreAnimatedStateSystem::IsRelevantImpl(UMovieSceneEntitySystemLinker* InLinker) const
{
	using namespace UE::MovieScene;

	return InLinker->EntityManager.ContainsComponent(FBuiltInComponentTypes::Get()->Tags.RestoreState);
}

void UMovieSceneRestorePreAnimatedStateSystem::OnLink()
{
	using namespace UE::MovieScene;
	FPreAnimatedStateExtension* ExistingExtension = Linker->FindExtension<FPreAnimatedStateExtension>();
	if (ExistingExtension)
	{
		PreAnimatedStateRef = ExistingExtension->AsShared();
	}
	else
	{
		// FPreAnimatedStateExtension automatically adds itself to the linker
		PreAnimatedStateRef = MakeShared<FPreAnimatedStateExtension>(Linker);
	}

	UMovieSceneCachePreAnimatedStateSystem* CacheSystem = Linker->LinkSystem<UMovieSceneCachePreAnimatedStateSystem>();
	Linker->SystemGraph.AddReference(this, CacheSystem);
}

void UMovieSceneRestorePreAnimatedStateSystem::OnUnlink()
{
	PreAnimatedStateRef = nullptr;
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
	Linker->SystemGraph.IteratePhase(ESystemPhase::Evaluation, ForEachSystem);

	IMovieScenePreAnimatedStateSystemInterface::FPreAnimationParameters Params{ &InPrerequisites, &Subsequents, PreAnimatedStateRef.Get() };

	// Iterate backwards restoring stale state
	for (int32 Index = Interfaces.Num()-1; Index >= 0; --Index)
	{
		Interfaces[Index]->RestorePreAnimatedState(Params);
	}

	Params.CacheExtension->ResetEntryInvalidation();
}
