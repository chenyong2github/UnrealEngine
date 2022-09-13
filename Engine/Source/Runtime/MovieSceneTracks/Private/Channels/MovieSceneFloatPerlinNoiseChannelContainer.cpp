// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneFloatPerlinNoiseChannelContainer.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneTracksComponentTypes.h"
#include "Misc/Guid.h"

bool UMovieSceneFloatPerlinNoiseChannelContainer::SupportsOverride(FName DefaultChannelTypeName) const
{
	return FMovieSceneFloatChannel::StaticStruct()->GetFName() == DefaultChannelTypeName;
}

void UMovieSceneFloatPerlinNoiseChannelContainer::ImportEntityImpl(const FMovieSceneChannelOverrideEntityImportParams& OverrideParams, const UE::MovieScene::FEntityImportParams& ImportParams, UE::MovieScene::FImportedEntity* OutImportedEntity)
{
	using namespace UE::MovieScene;

	const FMovieSceneTracksComponentTypes* TracksComponents = FMovieSceneTracksComponentTypes::Get();
	const FBuiltInComponentTypes* BuiltInComponents = FBuiltInComponentTypes::Get();

	TComponentTypeID<double> ResultComponent = OverrideParams.ResultComponent.ReinterpretCast<double>();

	OutImportedEntity->AddBuilder(
		FEntityBuilder()
		.Add(TracksComponents->FloatPerlinNoiseChannel, PerlinNoiseChannel.GetParams())
		.Add(ResultComponent, TNumericLimits<float>::Max())
	);
}

#if WITH_EDITOR
FMovieSceneChannelHandle UMovieSceneFloatPerlinNoiseChannelContainer::AddChannelProxy(FName ChannelName, FMovieSceneChannelProxyData& ProxyData, const FMovieSceneChannelMetaData& MetaData)
{
	return ProxyData.AddWithDefaultEditorData(PerlinNoiseChannel, MetaData);
}
#else
void UMovieSceneFloatPerlinNoiseChannelContainer::AddChannelProxy(FName ChannelName, FMovieSceneChannelProxyData& ProxyData)
{
	ProxyData.Add(PerlinNoiseChannel);
}
#endif

