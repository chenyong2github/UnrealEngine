// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieStreamerModule.h"
#include "MediaMovieStreamer.h"

#define LOCTEXT_NAMESPACE "FMediaMovieStreamerModule"

TSharedPtr<FMediaMovieStreamer> MovieStreamer;

const TSharedPtr<FMediaMovieStreamer> FMediaMovieStreamerModule::GetMovieStreamer()
{
	return MovieStreamer;
}

void FMediaMovieStreamerModule::StartupModule()
{
	MovieStreamer = MakeShareable(new FMediaMovieStreamer);
	FCoreDelegates::RegisterMovieStreamerDelegate.Broadcast(MovieStreamer);
}

void FMediaMovieStreamerModule::ShutdownModule()
{
	if (MovieStreamer.IsValid())
	{
		FCoreDelegates::UnRegisterMovieStreamerDelegate.Broadcast(MovieStreamer);
		MovieStreamer.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMediaMovieStreamerModule, MediaMovieStreamer)