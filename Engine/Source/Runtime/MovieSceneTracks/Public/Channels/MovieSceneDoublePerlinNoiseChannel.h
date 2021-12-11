// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "MovieSceneTracksComponentTypes.h"
#include "MovieSceneDoublePerlinNoiseChannel.generated.h"

USTRUCT()
struct MOVIESCENETRACKS_API FMovieSceneDoublePerlinNoiseChannel : public FMovieSceneChannel
{
	GENERATED_BODY()

	using CurveValueType = double;

	FMovieSceneDoublePerlinNoiseChannel();
	FMovieSceneDoublePerlinNoiseChannel(const FDoublePerlinNoiseParams& InDoublePerlinNoiseParams);

	/**
	* Evaluate this channel with the frame resolution
	*
	* @param InSeconds  The Frame second to evaluate at
	* @return A value to receive the PerlinNoise result
	*/
	double Evaluate(double InSeconds);

	const FDoublePerlinNoiseParams& GetParam() const { return DoublePerlinNoiseParams; }
	FDoublePerlinNoiseParams& GetParam() { return DoublePerlinNoiseParams; }

private:
	FDoublePerlinNoiseParams DoublePerlinNoiseParams;
};

template<>
struct TMovieSceneChannelTraits<FMovieSceneDoublePerlinNoiseChannel> : TMovieSceneChannelTraitsBase<FMovieSceneDoublePerlinNoiseChannel>
{
#if WITH_EDITOR

	/** FMovieSceneFloatPerlinNoiseChannel channels can have external values (ie, they can get their values from external objects for UI purposes) */
	typedef TMovieSceneExternalValue<double> ExtendedEditorDataType;

#endif
};
