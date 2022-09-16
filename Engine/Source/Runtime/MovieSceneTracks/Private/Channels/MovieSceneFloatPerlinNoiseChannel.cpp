// Copyright Epic Games, Inc. All Rights Reserved.

#include "Channels/MovieSceneFloatPerlinNoiseChannel.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"

FMovieSceneFloatPerlinNoiseChannel::FMovieSceneFloatPerlinNoiseChannel()
	: PerlinNoiseParams{}
{
}

FMovieSceneFloatPerlinNoiseChannel::FMovieSceneFloatPerlinNoiseChannel(const FPerlinNoiseParams& InFloatPerlinNoiseParams)
	: PerlinNoiseParams{ InFloatPerlinNoiseParams }
{
}

float FMovieSceneFloatPerlinNoiseChannel::Evaluate(double InSeconds) const
{
	float Result = FMath::PerlinNoise1D(InSeconds * PerlinNoiseParams.Frequency);
	Result *= PerlinNoiseParams.Amplitude;
	return Result;
}

bool FMovieSceneFloatPerlinNoiseChannel::Evaluate(const UMovieSceneSection* InSection, FFrameTime InTime, float& OutValue) const
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

