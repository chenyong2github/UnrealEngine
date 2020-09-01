// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePropertySystem.h"
#include "Systems/MovieScenePropertyInstantiator.h"

#include "EntitySystem/MovieSceneEntitySystemLinker.h"

UMovieScenePropertySystem::UMovieScenePropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	SystemExclusionContext |= UE::MovieScene::EEntitySystemContext::Interrogation;
}

void UMovieScenePropertySystem::OnLink()
{
	using namespace UE::MovieScene;

	// Never apply properties during evaluation. This code is necessary if derived types do support interrogation.
	if (!EnumHasAnyFlags(Linker->GetSystemContext(), EEntitySystemContext::Interrogation))
	{
		InstantiatorSystem = Linker->LinkSystem<UMovieScenePropertyInstantiatorSystem>();
		Linker->SystemGraph.AddReference(this, InstantiatorSystem);
	}
}

void UMovieScenePropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	// Never apply properties during evaluation. This code is necessary if derived types do support interrogation.
	if (EnumHasAnyFlags(Linker->GetSystemContext(), EEntitySystemContext::Interrogation))
	{
		return;
	}

	FPropertyStats Stats = InstantiatorSystem->GetStatsForProperty(CompositePropertyID);
	if (Stats.NumProperties > 0)
	{
		FPropertyRegistry* PropertyRegistry = &FBuiltInComponentTypes::Get()->PropertyRegistry;
		const FPropertyDefinition& Definition = PropertyRegistry->GetDefinition(CompositePropertyID);

		Definition.Handler->DispatchSetterTasks(Definition, PropertyRegistry->GetComposites(Definition), Stats, InPrerequisites, Subsequents, Linker);
	}
}