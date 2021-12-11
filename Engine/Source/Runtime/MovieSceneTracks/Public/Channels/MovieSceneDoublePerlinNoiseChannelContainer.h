// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Channels/MovieSceneChannelOverrideContainer.h"
#include "Channels/MovieSceneDoublePerlinNoiseChannel.h"
#include "MovieSceneDoublePerlinNoiseChannelContainer.generated.h"

/**
* Double perlin noise channel overriden container
*/
UCLASS(meta = (DisplayName = "Double Perlin Noise", ToolTip = "Override a channel to use double perlin noise"))
class  MOVIESCENETRACKS_API UMovieSceneDoublePerlinNoiseChannelContainer : public UMovieSceneChannelOverrideContainer
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FMovieSceneDoublePerlinNoiseChannel DoublePerlinNoiseChannel;

public:
	virtual void ImportEntityImpl(int32 ChannelIndex, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity) override final;
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override final;

	virtual const FMovieSceneChannel* GetChannel() const override final { return &DoublePerlinNoiseChannel; }
	virtual FMovieSceneChannel* GetChannel() override final { return &DoublePerlinNoiseChannel; }
};
