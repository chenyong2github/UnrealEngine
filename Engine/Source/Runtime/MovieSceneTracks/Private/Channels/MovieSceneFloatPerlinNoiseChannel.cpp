// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneFloatPerlinNoiseChannel.h"

FMovieSceneFloatPerlinNoiseChannel::FMovieSceneFloatPerlinNoiseChannel()
	: FloatPerlinNoiseParams{}
{
}

FMovieSceneFloatPerlinNoiseChannel::FMovieSceneFloatPerlinNoiseChannel(const FFloatPerlinNoiseParams& InFloatPerlinNoiseParams)
	: FloatPerlinNoiseParams{ InFloatPerlinNoiseParams }
{
}

float FMovieSceneFloatPerlinNoiseChannel::Evaluate(double InSeconds)
{
	float Result = FMath::PerlinNoise1D(InSeconds * FloatPerlinNoiseParams.Frequency);
	Result *= FloatPerlinNoiseParams.Amplitude;
	return Result;
}