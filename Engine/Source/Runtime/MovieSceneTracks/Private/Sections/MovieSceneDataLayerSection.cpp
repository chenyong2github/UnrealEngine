// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneDataLayerSection.h"
#include "MovieSceneTracksComponentTypes.h"
#include "WorldPartition/DataLayer/DataLayer.h"

UMovieSceneDataLayerSection::UMovieSceneDataLayerSection(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	DesiredState = EDataLayerState::Loaded;
	PrerollState = EDataLayerState::Loaded;
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::RestoreState);
}

EDataLayerState UMovieSceneDataLayerSection::GetDesiredState() const
{
	return DesiredState;
}

void UMovieSceneDataLayerSection::SetDesiredState(EDataLayerState InDesiredState)
{
	DesiredState = InDesiredState;
}

EDataLayerState UMovieSceneDataLayerSection::GetPrerollState() const
{
	return PrerollState;
}

void UMovieSceneDataLayerSection::SetPrerollState(EDataLayerState InPrerollState)
{
	PrerollState = InPrerollState;
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
