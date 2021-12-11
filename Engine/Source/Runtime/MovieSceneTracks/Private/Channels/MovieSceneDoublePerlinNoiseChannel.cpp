// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneDoublePerlinNoiseChannel.h"

FMovieSceneDoublePerlinNoiseChannel::FMovieSceneDoublePerlinNoiseChannel()
	: DoublePerlinNoiseParams{}
{
}

FMovieSceneDoublePerlinNoiseChannel::FMovieSceneDoublePerlinNoiseChannel(const FDoublePerlinNoiseParams& InDoublePerlinNoiseParams)
	: DoublePerlinNoiseParams{ InDoublePerlinNoiseParams }
{
}

double FMovieSceneDoublePerlinNoiseChannel::Evaluate(double InSeconds)
{
	double Result = static_cast<double>(FMath::PerlinNoise1D(InSeconds * DoublePerlinNoiseParams.Frequency));
	Result *= DoublePerlinNoiseParams.Amplitude;
	return Result;
}
