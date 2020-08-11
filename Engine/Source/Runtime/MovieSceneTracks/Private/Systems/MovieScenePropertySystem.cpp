// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/MovieScenePropertySystem.h"
#include "Systems/MovieScenePropertyInstantiator.h"

#include "EntitySystem/MovieSceneEntitySystemLinker.h"

UMovieScenePropertySystem::UMovieScenePropertySystem(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{}

void UMovieScenePropertySystem::OnLink()
{
	InstantiatorSystem = Linker->LinkSystem<UMovieScenePropertyInstantiatorSystem>();
	Linker->SystemGraph.AddReference(this, InstantiatorSystem);
}

void UMovieScenePropertySystem::OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents)
{
	using namespace UE::MovieScene;

	FPropertyStats Stats = InstantiatorSystem->GetStatsForProperty(CompositePropertyID);
	if (Stats.NumProperties > 0)
	{
		FPropertyRegistry* PropertyRegistry = &FBuiltInComponentTypes::Get()->PropertyRegistry;
		const FPropertyDefinition& Definition = PropertyRegistry->GetDefinition(CompositePropertyID);

		Definition.Handler->DispatchSetterTasks(Definition, PropertyRegistry->GetComposites(Definition), Stats, InPrerequisites, Subsequents, Linker);
	}
}