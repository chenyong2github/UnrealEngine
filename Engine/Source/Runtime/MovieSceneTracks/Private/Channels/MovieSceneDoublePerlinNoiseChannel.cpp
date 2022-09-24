// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneDoublePerlinNoiseChannel.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneDoublePerlinNoiseChannel)

FMovieSceneDoublePerlinNoiseChannel::FMovieSceneDoublePerlinNoiseChannel()
	: PerlinNoiseParams{}
{
}

FMovieSceneDoublePerlinNoiseChannel::FMovieSceneDoublePerlinNoiseChannel(const FPerlinNoiseParams& InPerlinNoiseParams)
	: PerlinNoiseParams{ InPerlinNoiseParams }
{
}

double FMovieSceneDoublePerlinNoiseChannel::Evaluate(double InSeconds) const
{
	double Result = static_cast<double>(FMath::PerlinNoise1D(InSeconds * PerlinNoiseParams.Frequency));
	Result *= PerlinNoiseParams.Amplitude;
	return Result;
}

bool FMovieSceneDoublePerlinNoiseChannel::Evaluate(const UMovieSceneSection* InSection, FFrameTime InTime, double& OutValue) const
{
	if (ensure(InSection))
	{
		UMovieScene* MovieScene = InSection->GetTypedOuter<UMovieScene>();
		if (MovieScene)
		{
			const FFrameRate TickResolution = MovieScene->GetTickResolution();
			const double Seconds = TickResolution.AsSeconds(InTime);
			OutValue = Evaluate(Seconds);
			return true;
		}
	}
	return false;
}

