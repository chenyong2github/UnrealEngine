// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphDefaultDataCaching.h"
#include "Graph/MovieGraphPipeline.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MoviePipelineQueue.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineBlueprintLibrary.h"
#include "Engine/GameViewportClient.h"

void UMovieGraphDefaultDataCaching::CacheDataPreJob(const FMovieGraphInitConfig& InInitConfig)
{
	// Turn off screen messages as some forms are drawn directly to final render targets
	// which will polute final frames.
	GAreScreenMessagesEnabled = false;

	// Turn off player viewport rendering to avoid overhead of an extra render.
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->bDisableWorldRendering = !InInitConfig.bRenderViewport;
	}

	ULevelSequence* RootSequence = Cast<ULevelSequence>(GetOwningGraph()->GetCurrentJob()->Sequence.TryLoad());
	if (RootSequence)
	{
		CacheLevelSequenceData(RootSequence);
	}
}

void UMovieGraphDefaultDataCaching::RestoreCachedDataPostJob()
{
	GAreScreenMessagesEnabled = true;
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		Viewport->bDisableWorldRendering = false;
	}
}

void UMovieGraphDefaultDataCaching::UpdateShotList()
{
	ULevelSequence* RootSequence = Cast<ULevelSequence>(GetOwningGraph()->GetCurrentJob()->Sequence.TryLoad());
	if (RootSequence)
	{
		bool bShotsChanged = false;
		UMoviePipelineBlueprintLibrary::UpdateJobShotListFromSequence(RootSequence, GetOwningGraph()->GetCurrentJob(), bShotsChanged);
	}
}

void UMovieGraphDefaultDataCaching::CacheLevelSequenceData(ULevelSequence* InSequence)
{
	// There is a reasonable chance that there exists a Level Sequence Actor in the world already set up to play this sequence.
	ALevelSequenceActor* ExistingActor = nullptr;

	for (auto It = TActorIterator<ALevelSequenceActor>(GetWorld()); It; ++It)
	{
		// Iterate through all of them in the event someone has multiple copies in the world on accident.
		if (It->GetSequence() == InSequence)
		{
			// Found it!
			ExistingActor = *It;

			// Stop it from playing if it's already playing.
			if (ExistingActor->GetSequencePlayer())
			{
				ExistingActor->GetSequencePlayer()->Stop();
			}
		}
	}

	LevelSequenceActor = ExistingActor;
	if (!LevelSequenceActor)
	{
		// Spawn a new level sequence
		LevelSequenceActor = GetWorld()->SpawnActor<ALevelSequenceActor>();
		check(LevelSequenceActor);
	}

	// Enforce settings.
	LevelSequenceActor->PlaybackSettings.LoopCount.Value = 0;
	LevelSequenceActor->PlaybackSettings.bAutoPlay = false;
	LevelSequenceActor->PlaybackSettings.bPauseAtEnd = true;
	LevelSequenceActor->PlaybackSettings.bRestoreState = true;

	// Ensure the (possibly new) Level Sequence Actor uses our sequence
	LevelSequenceActor->SetSequence(InSequence);

	// LevelSequenceActor->GetSequencePlayer()->SetTimeController(CustomSequenceTimeController);
	LevelSequenceActor->GetSequencePlayer()->Stop();

	LevelSequenceActor->GetSequencePlayer()->OnSequenceUpdated().AddUObject(this, &UMovieGraphDefaultDataCaching::OnSequenceEvaluated);

}

void UMovieGraphDefaultDataCaching::OnSequenceEvaluated(const UMovieSceneSequencePlayer& Player, FFrameTime CurrentTime, FFrameTime PreviousTime)
{
	// This callback exists for logging purposes. DO NOT HINGE LOGIC ON THIS CALLBACK
	// because this may get called multiple times per frame and may be the result of
	// a seek operation which is reverted before a frame is even rendered.
	UE_LOG(LogMovieRenderPipeline, VeryVerbose, TEXT("[GFrameCounter: %d] Sequence Evaluated. CurrentTime: %s PreviousTime: %s"), GFrameCounter, *LexToString(CurrentTime), *LexToString(PreviousTime));
}
