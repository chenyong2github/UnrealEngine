// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneFloatPerlinNoiseChannelContainer.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Misc/Guid.h"

void UMovieSceneFloatPerlinNoiseChannelContainer::ImportEntityImpl(int32 ChannelIndex, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	const FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();
	const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.Add(TracksComponents->FloatPerlinNoiseChannel, FloatPerlinNoiseChannel.GetParam())
		.Add(BuiltInComponents->DoubleResult[ChannelIndex], TNumericLimits<float>::Max())
	);
}

EMovieSceneChannelProxyType UMovieSceneFloatPerlinNoiseChannelContainer::CacheChannelProxy()
{
	//TODO in Phase 2
	return EMovieSceneChannelProxyType();
}
