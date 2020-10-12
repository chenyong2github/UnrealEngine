// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTimeHelpers.h"
#include "CoreTypes.h"
#include "Misc/FrameNumber.h"
#include "Misc/FrameRate.h"
#include "Misc/FrameTime.h"
#include "Misc/ScopedSlowTask.h"
#include "Sections/MovieSceneSubSection.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneChannel.h"

namespace UE
{
namespace MovieScene
{

TRange<FFrameNumber> MigrateFrameRange(const TRange<FFrameNumber>& SourceRange, FFrameRate SourceRate, FFrameRate DestinationRate)
{
	if (SourceRate == DestinationRate)
	{
		return SourceRange;
	}

	TRange<FFrameNumber> NewRange = TRange<FFrameNumber>::All();

	if (!SourceRange.GetLowerBound().IsOpen())
	{
		const FFrameNumber FrameNumber = ConvertFrameTime(SourceRange.GetLowerBoundValue(), SourceRate, DestinationRate).RoundToFrame();

		NewRange.SetLowerBound(
			SourceRange.GetLowerBound().IsExclusive()
			? TRangeBound<FFrameNumber>::Exclusive(FrameNumber)
			: TRangeBound<FFrameNumber>::Inclusive(FrameNumber)
		);
	}

	if (!SourceRange.GetUpperBound().IsOpen())
	{
		const FFrameNumber FrameNumber = ConvertFrameTime(SourceRange.GetUpperBoundValue(), SourceRate, DestinationRate).RoundToFrame();

		NewRange.SetUpperBound(
			SourceRange.GetUpperBound().IsExclusive()
			? TRangeBound<FFrameNumber>::Exclusive(FrameNumber)
			: TRangeBound<FFrameNumber>::Inclusive(FrameNumber)
		);
	}

	return NewRange;
}

void MigrateFrameTimes(FFrameRate SourceRate, FFrameRate DestinationRate, UMovieSceneSection* Section)
{
	Section->Modify();

	TRangeBound<FFrameNumber> NewLowerBound, NewUpperBound;

	if (Section->HasStartFrame())
	{
		FFrameNumber NewLowerBoundFrame = ConvertFrameTime(Section->GetInclusiveStartFrame(), SourceRate, DestinationRate).FloorToFrame();
		NewLowerBound = TRangeBound<FFrameNumber>::Inclusive(NewLowerBoundFrame);
	}

	if (Section->HasEndFrame())
	{
		FFrameNumber NewUpperBoundValue = ConvertFrameTime(Section->GetExclusiveEndFrame(), SourceRate, DestinationRate).FloorToFrame();
		NewUpperBound = TRangeBound<FFrameNumber>::Exclusive(NewUpperBoundValue);
	}

	Section->SetRange(TRange<FFrameNumber>(NewLowerBound, NewUpperBound));

	if (Section->GetPreRollFrames() > 0)
	{
		FFrameNumber NewPreRollFrameCount = ConvertFrameTime(FFrameTime(Section->GetPreRollFrames()), SourceRate, DestinationRate).FloorToFrame();
		Section->SetPreRollFrames(NewPreRollFrameCount.Value);
	}

	if (Section->GetPostRollFrames() > 0)
	{
		FFrameNumber NewPostRollFrameCount = ConvertFrameTime(FFrameTime(Section->GetPostRollFrames()), SourceRate, DestinationRate).FloorToFrame();
		Section->SetPostRollFrames(NewPostRollFrameCount.Value);
	}

	if (Section->IsA(UMovieSceneSubSection::StaticClass()))
	{
		UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
		if (SubSection->Parameters.StartFrameOffset.Value > 0)
		{
			FFrameNumber NewStartFrameOffset = ConvertFrameTime(FFrameTime(SubSection->Parameters.StartFrameOffset), SourceRate, DestinationRate).FloorToFrame();
			SubSection->Parameters.StartFrameOffset = NewStartFrameOffset;
		}
	}

	Section->Easing.AutoEaseInDuration    = ConvertFrameTime(Section->Easing.AutoEaseInDuration,    SourceRate, DestinationRate).FloorToFrame().Value;
	Section->Easing.AutoEaseOutDuration   = ConvertFrameTime(Section->Easing.AutoEaseOutDuration,   SourceRate, DestinationRate).FloorToFrame().Value;
	Section->Easing.ManualEaseInDuration  = ConvertFrameTime(Section->Easing.ManualEaseInDuration,  SourceRate, DestinationRate).FloorToFrame().Value;
	Section->Easing.ManualEaseOutDuration = ConvertFrameTime(Section->Easing.ManualEaseOutDuration, SourceRate, DestinationRate).FloorToFrame().Value;

	for (const FMovieSceneChannelEntry& Entry : Section->GetChannelProxy().GetAllEntries())
	{
		for (FMovieSceneChannel* Channel : Entry.GetChannels())
		{
			Channel->ChangeFrameResolution(SourceRate, DestinationRate);
		}
	}
}

void MigrateFrameTimes(FFrameRate SourceRate, FFrameRate DestinationRate, UMovieSceneTrack* Track)
{
	FScopedSlowTask SlowTask(Track->GetAllSections().Num());

	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		SlowTask.EnterProgressFrame();
		MigrateFrameTimes(SourceRate, DestinationRate, Section);
	}
}

void TimeHelpers::MigrateFrameTimes(FFrameRate SourceRate, FFrameRate DestinationRate, UMovieScene* MovieScene)
{
	int32 TotalNumTracks = MovieScene->GetMasterTracks().Num() + (MovieScene->GetCameraCutTrack() ? 1 : 0);
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		TotalNumTracks += Binding.GetTracks().Num();
	}

	FScopedSlowTask SlowTask(TotalNumTracks, NSLOCTEXT("MovieScene", "ChangingTickResolution", "Migrating sequence frame timing"));
	SlowTask.MakeDialogDelayed(0.25f, true);

	MovieScene->Modify();

	MovieScene->SetPlaybackRange(MigrateFrameRange(MovieScene->GetPlaybackRange(), SourceRate, DestinationRate));
#if WITH_EDITORONLY_DATA
	MovieScene->SetSelectionRange(MigrateFrameRange(MovieScene->GetSelectionRange(), SourceRate, DestinationRate));
#endif

	for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
	{
		SlowTask.EnterProgressFrame();
		UE::MovieScene::MigrateFrameTimes(SourceRate, DestinationRate, Track);
	}

	if (UMovieSceneTrack* Track = MovieScene->GetCameraCutTrack())
	{
		SlowTask.EnterProgressFrame();
		UE::MovieScene::MigrateFrameTimes(SourceRate, DestinationRate, Track);
	}

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			SlowTask.EnterProgressFrame();
			UE::MovieScene::MigrateFrameTimes(SourceRate, DestinationRate, Track);
		}
	}

	MovieScene->SetTickResolutionDirectly(DestinationRate);
}

} // namespace MovieScene
} // namespace UE