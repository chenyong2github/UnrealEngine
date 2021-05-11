// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaMovieStreamerModule.h"

#include "MediaMovieAssets.h"
#include "MediaMovieStreamer.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FMediaMovieStreamerModule"

TSharedPtr<FMediaMovieStreamer, ESPMode::ThreadSafe> MovieStreamer;
TWeakObjectPtr<UMediaMovieAssets> MovieAssets;

UMediaMovieAssets* FMediaMovieStreamerModule::GetMovieAssets()
{
	return MovieAssets.Get();
}

const TSharedPtr<FMediaMovieStreamer, ESPMode::ThreadSafe> FMediaMovieStreamerModule::GetMovieStreamer()
{
	return MovieStreamer;
}

void FMediaMovieStreamerModule::StartupModule()
{
	// Create MovieAssets.
	MovieAssets = NewObject<UMediaMovieAssets>();
	MovieAssets->AddToRoot();

	// Create MovieStreamer.
	MovieStreamer = MakeShareable(new FMediaMovieStreamer);
	FCoreDelegates::RegisterMovieStreamerDelegate.Broadcast(MovieStreamer);
}

void FMediaMovieStreamerModule::ShutdownModule()
{
	// Shutdown MovieStreamer.
	if (MovieStreamer.IsValid())
	{
		FCoreDelegates::UnRegisterMovieStreamerDelegate.Broadcast(MovieStreamer);
		MovieStreamer.Reset();
	}

	// Shutdown MovieAssets.
	if (MovieAssets.IsValid())
	{
		MovieAssets->RemoveFromRoot();
		MovieAssets.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FMediaMovieStreamerModule, MediaMovieStreamer)