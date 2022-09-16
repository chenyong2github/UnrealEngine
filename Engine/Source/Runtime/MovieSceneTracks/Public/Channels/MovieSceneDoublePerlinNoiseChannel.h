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
	FMovieSceneDoublePerlinNoiseChannel(const FPerlinNoiseParams& InDoublePerlinNoiseParams);

	/**
	* Evaluate this channel at the given time
	*
	* @param InSeconds  The time, in seconds, to evaluate at
	* @return A value to receive the PerlinNoise result
	*/
	double Evaluate(double InSeconds) const;

	/**
	 * Evaluate this channel at the given time
	 *
	 * @param InSection  The section that contains this channel, used to lookup the sequence's tick resolution
	 * @param InTime     The time, in ticks, to evaluate at
	 * @param OutValue   The evaluated noise value
	 * @return           Whether the noise was successfully evaluated
	 */
	bool Evaluate(const UMovieSceneSection* InSection, FFrameTime InTime, double& OutValue) const;

	/** The noise parameters */
	UPROPERTY(EditAnywhere, Category = "Perlin Noise")
	FPerlinNoiseParams PerlinNoiseParams;
};

template<>
struct TMovieSceneChannelTraits<FMovieSceneDoublePerlinNoiseChannel> : TMovieSceneChannelTraitsBase<FMovieSceneDoublePerlinNoiseChannel>
{
#if WITH_EDITOR

	/** Perlin noise channels can have external values (ie, they can get their values from external objects for UI purposes) */
	typedef TMovieSceneExternalValue<double> ExtendedEditorDataType;

#endif
};

inline bool EvaluateChannel(const UMovieSceneSection* InSection, const FMovieSceneDoublePerlinNoiseChannel* InChannel, FFrameTime InTime, double& OutValue)
{
	return InChannel->Evaluate(InSection, InTime, OutValue);
}

