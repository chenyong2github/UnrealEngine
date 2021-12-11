// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneDoublePerlinNoiseChannelContainer.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Misc/Guid.h"

void UMovieSceneDoublePerlinNoiseChannelContainer::ImportEntityImpl(int32 ChannelIndex, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	const FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();
	const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.Add(TracksComponents->DoublePerlinNoiseChannel, DoublePerlinNoiseChannel.GetParam())
		.Add(BuiltInComponents->DoubleResult[ChannelIndex], TNumericLimits<double>::Max())
	);
}

EMovieSceneChannelProxyType UMovieSceneDoublePerlinNoiseChannelContainer::CacheChannelProxy()
{
	//TODO in Phase 2
	return EMovieSceneChannelProxyType();
}
