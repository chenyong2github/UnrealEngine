// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieScenePrimitiveMaterialSection.h"
#include "Tracks/MovieScenePrimitiveMaterialTrack.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Materials/MaterialInterface.h"
#include "EntitySystem/MovieSceneEntityBuilder.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "MovieSceneTracksComponentTypes.h"


UMovieScenePrimitiveMaterialSection::UMovieScenePrimitiveMaterialSection(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	bSupportsInfiniteRange = true;
	EvalOptions.EnableAndSetCompletionMode(EMovieSceneCompletionMode::ProjectDefault);
	SetRange(TRange<FFrameNumber>::All());

	MaterialChannel.SetPropertyClass(UMaterialInterface::StaticClass());

#if WITH_EDITOR
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MaterialChannel, FMovieSceneChannelMetaData(), TMovieSceneExternalValue<UObject*>::Make());
#else
	ChannelProxy = MakeShared<FMovieSceneChannelProxy>(MaterialChannel);
#endif
}

void UMovieScenePrimitiveMaterialSection::ImportEntityImpl(UMovieSceneEntitySystemLinker* Linker, const FEntityImportParams& ImportParams, FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	UMovieScenePrimitiveMaterialTrack* Track = GetTypedOuter<UMovieScenePrimitiveMaterialTrack>();
	check(Track);

	const int32 MaterialIndex = Track->GetMaterialIndex();

	FBuiltInComponentTypes* BuiltInComponentTypes = FBuiltInComponentTypes::Get();
	FMovieSceneTracksComponentTypes* TracksComponentTypes = FMovieSceneTracksComponentTypes::Get();

	FGuid ObjectBindingID = ImportParams.GetObjectBindingID();

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.AddConditional(BuiltInComponentTypes->GenericObjectBinding, ObjectBindingID, ObjectBindingID.IsValid())
		.Add(BuiltInComponentTypes->ObjectPathChannel, &MaterialChannel)
		.Add(TracksComponentTypes->ComponentMaterialIndex, MaterialIndex)
	);
}