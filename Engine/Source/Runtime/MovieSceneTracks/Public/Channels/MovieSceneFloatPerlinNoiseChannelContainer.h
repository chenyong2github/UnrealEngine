// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Channels/MovieSceneChannelOverrideContainer.h"
#include "Channels/MovieSceneFloatPerlinNoiseChannel.h"
#include "MovieSceneFloatPerlinNoiseChannelContainer.generated.h"

/**
* Float perlin noise channel overriden container
*/
UCLASS(meta = (DisplayName = "Float Perlin Noise", ToolTip = "Override a channel to use float perlin noise"))
class MOVIESCENETRACKS_API UMovieSceneFloatPerlinNoiseChannelContainer : public UMovieSceneChannelOverrideContainer
{
	GENERATED_BODY()

private:
	UPROPERTY()
	FMovieSceneFloatPerlinNoiseChannel FloatPerlinNoiseChannel;

public:
	virtual void ImportEntityImpl(int32 ChannelIndex, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity) override final;
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override final;

	virtual const FMovieSceneChannel* GetChannel() const override final { return &FloatPerlinNoiseChannel; }
	virtual FMovieSceneChannel* GetChannel() override final { return &FloatPerlinNoiseChannel; }
};
