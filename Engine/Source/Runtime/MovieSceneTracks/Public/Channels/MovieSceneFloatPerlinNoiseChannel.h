// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Channels/MovieSceneChannel.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelTraits.h"
#include "MovieSceneTracksComponentTypes.h"
#include "MovieSceneFloatPerlinNoiseChannel.generated.h"

USTRUCT()
struct MOVIESCENETRACKS_API FMovieSceneFloatPerlinNoiseChannel : public FMovieSceneChannel
{
	GENERATED_BODY()

	using CurveValueType = float;

	FMovieSceneFloatPerlinNoiseChannel();
	FMovieSceneFloatPerlinNoiseChannel(const FPerlinNoiseParams& InFloatPerlinNoiseParams);

	/**
	* Evaluate this channel with the frame resolution
	*
	* @param InSeconds  The Frame second to evaluate at
	* @return A value to receive the PerlinNoise result
	*/
	float Evaluate(double InSeconds) const;

	/**
	 * Evaluate this channel at the given time
	 *
	 * @param InSection  The section that contains this channel, used to lookup the sequence's tick resolution
	 * @param InTime     The time, in ticks, to evaluate at
	 * @param OutValue   The evaluated noise value
	 * @return           Whether the noise was successfully evaluated
	 */
	bool Evaluate(const UMovieSceneSection* InSection, FFrameTime InTime, float& OutValue) const;

	/** The noise parameters */
	UPROPERTY(EditAnywhere, Category="Perlin Noise")
	FPerlinNoiseParams PerlinNoiseParams;
};

template<>
struct TMovieSceneChannelTraits<FMovieSceneFloatPerlinNoiseChannel> : TMovieSceneChannelTraitsBase<FMovieSceneFloatPerlinNoiseChannel>
{
#if WITH_EDITOR

	/** Perlin noise channels can have external values (ie, they can get their values from external objects for UI purposes) */
	typedef TMovieSceneExternalValue<float> ExtendedEditorDataType;

#endif
};

inline bool EvaluateChannel(const UMovieSceneSection* InSection, const FMovieSceneFloatPerlinNoiseChannel* InChannel, FFrameTime InTime, float& OutValue)
{
	return InChannel->Evaluate(InSection, InTime, OutValue);
}

