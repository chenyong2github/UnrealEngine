// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeOfDayActor.h"
#include "Engine/ActorChannel.h"
#include "Net/UnrealNetwork.h"
#include "LevelSequenceActor.h"
#include "MovieSceneTimeHelpers.h"

ATimeOfDayActor::ATimeOfDayActor(const FObjectInitializer& Init)
: Super(Init)
, bRunDayCycle(true)
, DayLength(24, 0, 0, 0, false)
, TimePerCycle(0, 5, 0, 0, false)
, InitialTimeOfDay(6, 0, 0, 0, false)
{
	USceneComponent* SceneRootComponent = CreateDefaultSubobject<USceneComponent>(USceneComponent::GetDefaultSceneRootVariableName());
	SetRootComponent(SceneRootComponent);

#if WITH_EDITORONLY_DATA
	TimeOfDayPreview = FTimecode(6, 0, 0, 0, false);
#endif
}

void ATimeOfDayActor::PostInitializeComponents()
{
	// Initialize the player with our settings first so that the LevelSequenceActor's
	// PostInitializeComponents sequence player initialization is skipped.
	InitializeSequencePlayer();
	
	Super::PostInitializeComponents();
}

void ATimeOfDayActor::BeginPlay()
{
	// Override playback settings with our own based on the
	// day cycle properties.
	PlaybackSettings = GetPlaybackSettings();
	
	Super::BeginPlay();

	// Day cycle playback settings will always play. Pause if RunDayCycle is
	// off to allow sequence spawnables and settings to be set from initial
	// time of day.
	if (!bRunDayCycle)
	{
		SequencePlayer->Pause();
	}
}

void ATimeOfDayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ATimeOfDayActor::RewindForReplay()
{
	Super::RewindForReplay();
}

void ATimeOfDayActor::InitializeSequencePlayer()
{
	if (LevelSequenceAsset && GetWorld()->IsGameWorld())
	{
		// Level sequence is already loaded. Initialize the player if it's not already initialized with this sequence
		if (LevelSequenceAsset != SequencePlayer->GetSequence())
		{
			SequencePlayer->Initialize(LevelSequenceAsset, GetLevel(), GetPlaybackSettings(), FLevelSequenceCameraSettings());
		}
	}
}

FMovieSceneSequencePlaybackSettings ATimeOfDayActor::GetPlaybackSettings() const
{
	FMovieSceneSequencePlaybackSettings Settings;
	Settings.bAutoPlay = true;
	Settings.LoopCount.Value = -1; // Loop indefinitely
	Settings.PlayRate = 1.0f;

	if (!LevelSequenceAsset)
	{
		return Settings;
	}

	// Update the PlayRate and StartOffset from the DayCycle settings.
	if (const UMovieScene* MovieScene = LevelSequenceAsset->GetMovieScene())
	{
		const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const FFrameNumber FramesPerCycle = TimePerCycle.ToFrameNumber(DisplayRate);
		const FQualifiedFrameTime DayFrameTime(TimePerCycle, DisplayRate);
		
		const TRange<FFrameNumber> MoviePlaybackRange = MovieScene->GetPlaybackRange();
		if (MoviePlaybackRange.GetLowerBound().IsClosed() && MoviePlaybackRange.GetUpperBound().IsClosed() && FramesPerCycle.Value > 0)
		{
			const FFrameNumber SrcStartFrame = UE::MovieScene::DiscreteInclusiveLower(MoviePlaybackRange);
			const FFrameNumber SrcEndFrame   = UE::MovieScene::DiscreteExclusiveUpper(MoviePlaybackRange);

			const FFrameTime EndingTime = ConvertFrameTime(SrcEndFrame, TickResolution, DisplayRate);
			const FFrameNumber StartingFrame = ConvertFrameTime(SrcStartFrame, TickResolution, DisplayRate).FloorToFrame();
			const FFrameNumber EndingFrame   = EndingTime.FloorToFrame();
			
			const FQualifiedFrameTime MovieFrameTime(FTimecode(0,0,0,EndingFrame.Value - StartingFrame.Value, false), DisplayRate);
			Settings.PlayRate = MovieFrameTime.AsSeconds() / DayFrameTime.AsSeconds();

			const FFrameNumber FramesDayLength = DayLength.ToFrameNumber(DisplayRate);
			const FFrameNumber FramesStartTime = InitialTimeOfDay.ToFrameNumber(DisplayRate);
			if (FramesDayLength.Value > 0)
			{
				const FTimecode InitialTimeOfDayCorrected(0, 0, 0, FramesStartTime.Value % FramesDayLength.Value, false);
				const FQualifiedFrameTime DayLengthTime(DayLength, DisplayRate);
				const FQualifiedFrameTime StartTime(InitialTimeOfDayCorrected, DisplayRate);
				const double StartTimeRatio = StartTime.AsSeconds() / DayLengthTime.AsSeconds();

				// StartTime is in seconds in movie sequence time.
				Settings.StartTime = StartTimeRatio * MovieFrameTime.AsSeconds();
			}
		}
	}

	return Settings;
}


