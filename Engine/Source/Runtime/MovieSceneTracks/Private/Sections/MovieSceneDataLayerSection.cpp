// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneDataLayerSection.h"
#include "MovieSceneTracksComponentTypes.h"
#include "WorldPartition/DataLayer/DataLayer.h"

UMovieSceneDataLayerSection::UMovieSceneDataLayerSection(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	DesiredState = EDataLayerRuntimeState::Activated;
	PrerollState = EDataLayerRuntimeState::Activated;
	bFlushOnUnload = false;
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::RestoreState);
}

EDataLayerRuntimeState UMovieSceneDataLayerSection::GetDesiredState() const
{
	return DesiredState;
}

void UMovieSceneDataLayerSection::SetDesiredState(EDataLayerRuntimeState InDesiredState)
{
	DesiredState = InDesiredState;
}

EDataLayerRuntimeState UMovieSceneDataLayerSection::GetPrerollState() const
{
	return PrerollState;
}

void UMovieSceneDataLayerSection::SetPrerollState(EDataLayerRuntimeState InPrerollState)
{
	PrerollState = InPrerollState;
}

bool UMovieSceneDataLayerSection::GetFlushOnUnload() const
{
	return bFlushOnUnload;
}

void UMovieSceneDataLayerSection::SetFlushOnUnload(bool bInFlushOnUnload)
{
	bFlushOnUnload = bInFlushOnUnload;
}

void UMovieSceneDataLayerSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	FMovieSceneDataLayerComponentData ComponentData{decltype(FMovieSceneDataLayerComponentData::Section)(this) };

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.Add(FMovieSceneTracksComponentTypes::Get()->DataLayer, ComponentData)
	);
}
