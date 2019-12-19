// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTimeHelpers.h"
#include "Evaluation/MovieSceneEvaluationTemplate.h"
#include "Misc/FrameRate.h"
#include "Logging/MessageLog.h"

TWeakObjectPtr<UMovieSceneSubSection> UMovieSceneSubSection::TheRecordingSection;

float DeprecatedMagicNumber = TNumericLimits<float>::Lowest();

/* UMovieSceneSubSection structors
 *****************************************************************************/

UMovieSceneSubSection::UMovieSceneSubSection()
	: StartOffset_DEPRECATED(DeprecatedMagicNumber)
	, TimeScale_DEPRECATED(DeprecatedMagicNumber)
	, PrerollTime_DEPRECATED(DeprecatedMagicNumber)
{
}

FMovieSceneSequenceTransform UMovieSceneSubSection::OuterToInnerTransform() const
{
	UMovieSceneSequence* SequencePtr   = GetSequence();
	if (!SequencePtr)
	{
		return FMovieSceneSequenceTransform();
	}

	UMovieScene* MovieScenePtr = SequencePtr->GetMovieScene();

	TRange<FFrameNumber> SubRange = GetRange();
	if (!MovieScenePtr || SubRange.GetLowerBound().IsOpen())
	{
		return FMovieSceneSequenceTransform();
	}

	const FFrameRate   InnerFrameRate = MovieScenePtr->GetTickResolution();
	const FFrameRate   OuterFrameRate = GetTypedOuter<UMovieScene>()->GetTickResolution();
	const float        FrameRateScale = (OuterFrameRate == InnerFrameRate) ? 1.f : (InnerFrameRate / OuterFrameRate).AsDecimal();

	const TRange<FFrameNumber> MovieScenePlaybackRange = GetValidatedInnerPlaybackRange(Parameters, *MovieScenePtr);
	const FFrameNumber InnerStartTime = MovieScene::DiscreteInclusiveLower(MovieScenePlaybackRange);
	const FFrameNumber OuterStartTime = MovieScene::DiscreteInclusiveLower(SubRange);

	// This is the transform for the "placement" (position and scaling) of the sub-sequence.
	FMovieSceneTimeTransform LinearTransform =
		// Inner play offset
		FMovieSceneTimeTransform(InnerStartTime)
		// Inner play rate
		* FMovieSceneTimeTransform(0, Parameters.TimeScale * FrameRateScale)
		// Outer section start time
		* FMovieSceneTimeTransform(-OuterStartTime);
	
	if (!Parameters.bCanLoop)
	{
		return FMovieSceneSequenceTransform(LinearTransform);
	}
	else
	{
		const FFrameNumber InnerEndTime = MovieScene::DiscreteExclusiveUpper(MovieScenePlaybackRange);
		const FMovieSceneTimeWarping LoopingTransform(InnerStartTime, InnerEndTime);
		LinearTransform = FMovieSceneTimeTransform(Parameters.FirstLoopStartFrameOffset) * LinearTransform;

		FMovieSceneSequenceTransform Result;
		Result.NestedTransforms.Add(FMovieSceneNestedSequenceTransform(LinearTransform, LoopingTransform));
		return Result;
	}
}

bool UMovieSceneSubSection::GetValidatedInnerPlaybackRange(TRange<FFrameNumber>& OutInnerPlaybackRange) const
{
	UMovieSceneSequence* SequencePtr = GetSequence();
	if (SequencePtr != nullptr)
	{
		UMovieScene* MovieScenePtr = SequencePtr->GetMovieScene();
		if (MovieScenePtr != nullptr)
		{
			OutInnerPlaybackRange = GetValidatedInnerPlaybackRange(Parameters, *MovieScenePtr);
			return true;
		}
	}
	return false;
}

TRange<FFrameNumber> UMovieSceneSubSection::GetValidatedInnerPlaybackRange(const FMovieSceneSectionParameters& SubSectionParameters, const UMovieScene& InnerMovieScene)
{
	const TRange<FFrameNumber> InnerPlaybackRange = InnerMovieScene.GetPlaybackRange();
	TRangeBound<FFrameNumber> ValidatedLowerBound = InnerPlaybackRange.GetLowerBound();
	TRangeBound<FFrameNumber> ValidatedUpperBound = InnerPlaybackRange.GetUpperBound();
	if (ValidatedLowerBound.IsClosed() && ValidatedUpperBound.IsClosed())
	{
		const FFrameRate TickResolution = InnerMovieScene.GetTickResolution();
		const FFrameRate DisplayRate = InnerMovieScene.GetDisplayRate();
		const FFrameNumber OneFrameInTicks = FFrameRate::TransformTime(FFrameTime(1), DisplayRate, TickResolution).FloorToFrame();

		ValidatedLowerBound.SetValue(ValidatedLowerBound.GetValue() + SubSectionParameters.StartFrameOffset);
		ValidatedUpperBound.SetValue(FMath::Max(ValidatedUpperBound.GetValue() - SubSectionParameters.EndFrameOffset, ValidatedLowerBound.GetValue() + OneFrameInTicks));
		return TRange<FFrameNumber>(ValidatedLowerBound, ValidatedUpperBound);
	}
	return InnerPlaybackRange;
}

FString UMovieSceneSubSection::GetPathNameInMovieScene() const
{
	UMovieScene* OuterMovieScene = GetTypedOuter<UMovieScene>();
	check(OuterMovieScene);
	return GetPathName(OuterMovieScene);
}

FMovieSceneSequenceID UMovieSceneSubSection::GetSequenceID() const
{
	FString FullPath = GetPathNameInMovieScene();
	if (SubSequence)
	{
		FullPath += TEXT(" / ");
		FullPath += SubSequence->GetPathName();
	}

	return FMovieSceneSequenceID(FCrc::Strihash_DEPRECATED(*FullPath));
}

void UMovieSceneSubSection::PostLoad()
{
	FFrameRate LegacyFrameRate = GetLegacyConversionFrameRate();

	TOptional<double> StartOffsetToUpgrade;
	if (StartOffset_DEPRECATED != DeprecatedMagicNumber)
	{
		StartOffsetToUpgrade = StartOffset_DEPRECATED;

		StartOffset_DEPRECATED = DeprecatedMagicNumber;
	}
	else if (Parameters.StartOffset_DEPRECATED != 0.f)
	{
		StartOffsetToUpgrade = Parameters.StartOffset_DEPRECATED;
	}

	if (StartOffsetToUpgrade.IsSet())
	{
		FFrameNumber StartFrame = UpgradeLegacyMovieSceneTime(this, LegacyFrameRate, StartOffsetToUpgrade.GetValue());
		Parameters.StartFrameOffset = StartFrame;
	}

	if (TimeScale_DEPRECATED != DeprecatedMagicNumber)
	{
		Parameters.TimeScale = TimeScale_DEPRECATED;

		TimeScale_DEPRECATED = DeprecatedMagicNumber;
	}

	if (PrerollTime_DEPRECATED != DeprecatedMagicNumber)
	{
		Parameters.PrerollTime_DEPRECATED = PrerollTime_DEPRECATED;

		PrerollTime_DEPRECATED = DeprecatedMagicNumber;
	}

	// Pre and post roll is now supported generically
	if (Parameters.PrerollTime_DEPRECATED > 0.f)
	{
		FFrameNumber ClampedPreRollFrames = UpgradeLegacyMovieSceneTime(this, LegacyFrameRate, Parameters.PrerollTime_DEPRECATED);
		SetPreRollFrames(ClampedPreRollFrames.Value);
	}

	if (Parameters.PostrollTime_DEPRECATED > 0.f)
	{
		FFrameNumber ClampedPostRollFrames = UpgradeLegacyMovieSceneTime(this, LegacyFrameRate, Parameters.PostrollTime_DEPRECATED);
		SetPreRollFrames(ClampedPostRollFrames.Value);
	}

	Super::PostLoad();
}

void UMovieSceneSubSection::SetSequence(UMovieSceneSequence* Sequence)
{
	SubSequence = Sequence;

#if WITH_EDITOR
	OnSequenceChangedDelegate.ExecuteIfBound(SubSequence);
#endif
}

UMovieSceneSequence* UMovieSceneSubSection::GetSequence() const
{
	// when recording we need to act as if we have no sequence
	// the sequence is patched at the end of recording
	if(GetRecordingSection() == this)
	{
		return nullptr;
	}
	else
	{
		return SubSequence;
	}
}

UMovieSceneSubSection* UMovieSceneSubSection::GetRecordingSection()
{
	// check if the section is still valid and part of a track (i.e. it has not been deleted or GCed)
	if(TheRecordingSection.IsValid())
	{
		UMovieSceneTrack* TrackOuter = Cast<UMovieSceneTrack>(TheRecordingSection->GetOuter());
		if(TrackOuter)
		{
			if(TrackOuter->HasSection(*TheRecordingSection.Get()))
			{
				return TheRecordingSection.Get();
			}
		}
	}

	return nullptr;
}

void UMovieSceneSubSection::SetAsRecording(bool bRecord)
{
	if(bRecord)
	{
		TheRecordingSection = this;
	}
	else
	{
		TheRecordingSection = nullptr;
	}
}

bool UMovieSceneSubSection::IsSetAsRecording()
{
	return GetRecordingSection() != nullptr;
}

AActor* UMovieSceneSubSection::GetActorToRecord()
{
	UMovieSceneSubSection* RecordingSection = GetRecordingSection();
	if(RecordingSection)
	{
		return RecordingSection->ActorToRecord.Get();
	}

	return nullptr;
}

#if WITH_EDITOR
void UMovieSceneSubSection::PreEditChange(UProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UMovieSceneSubSection, SubSequence))
	{
		// Store the current subsequence in case it needs to be restored in PostEditChangeProperty because the new value would introduce a circular dependency
		PreviousSubSequence = SubSequence;
	}

	return Super::PreEditChange(PropertyAboutToChange);
}

void UMovieSceneSubSection::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMovieSceneSubSection, SubSequence))
	{
		UMovieSceneSubTrack* TrackOuter = Cast<UMovieSceneSubTrack>(GetOuter());
		if (SubSequence && TrackOuter && TrackOuter->ContainsSequence(*SubSequence, true, this))
		{
			UE_LOG(LogMovieScene, Error, TEXT("Invalid level sequence %s. There could be a circular dependency."), *SubSequence->GetDisplayName().ToString());

			// Restore to the previous sub sequence because there was a circular dependency
			SubSequence = PreviousSubSequence;
		}

		PreviousSubSequence = nullptr;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	// recreate runtime instance when sequence is changed
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMovieSceneSubSection, SubSequence))
	{
		OnSequenceChangedDelegate.ExecuteIfBound(SubSequence);
	}
}
#endif

UMovieSceneSection* UMovieSceneSubSection::SplitSection( FQualifiedFrameTime SplitTime, bool bDeleteKeys)
{
	// GetRange is in owning sequence resolution so we check against the incoming SplitTime without converting it.
	TRange<FFrameNumber> InitialRange = GetRange();
	if ( !InitialRange.Contains(SplitTime.Time.FrameNumber) )
	{
		return nullptr;
	}

	FFrameNumber InitialStartOffset = Parameters.StartFrameOffset;

	UMovieSceneSubSection* NewSection = Cast<UMovieSceneSubSection>( UMovieSceneSection::SplitSection( SplitTime, bDeleteKeys ) );
	if ( NewSection )
	{
		if (InitialRange.GetLowerBound().IsClosed())
		{
			// Sections need their offsets calculated in their local resolution. Different sequences can have different tick resolutions 
			// so we need to transform from the parent resolution to the local one before splitting them.
			FFrameRate LocalTickResolution;
			if (GetSequence())
			{
				LocalTickResolution = GetSequence()->GetMovieScene()->GetTickResolution();
			}
			else
			{
				UMovieScene* OuterScene = GetTypedOuter<UMovieScene>();
				if (OuterScene)
				{
					LocalTickResolution = OuterScene->GetTickResolution();
				}
			}

			FFrameNumber LocalResolutionStartOffset = FFrameRate::TransformTime(SplitTime.Time.GetFrame() - MovieScene::DiscreteInclusiveLower(InitialRange), SplitTime.Rate, LocalTickResolution).FrameNumber;

			FFrameNumber NewStartOffset = LocalResolutionStartOffset * Parameters.TimeScale;
			NewStartOffset += InitialStartOffset;

			if (NewStartOffset >= 0)
			{
				NewSection->Parameters.StartFrameOffset = NewStartOffset.Value;
			}
		}

		return NewSection;
	}

	// Restore original offset modified by splitting
	Parameters.StartFrameOffset = InitialStartOffset;

	return nullptr;
}

TOptional<TRange<FFrameNumber> > UMovieSceneSubSection::GetAutoSizeRange() const
{
	if (SubSequence && SubSequence->GetMovieScene())
	{
		// We probably want to just auto-size the section to the sub-sequence's scaled playback range... if this section
		// is looping, however, it's hard to know what we want to do.
		FMovieSceneTimeTransform InnerToOuter = OuterToInnerTransform().InverseLinearOnly();
		UMovieScene* InnerMovieScene = SubSequence->GetMovieScene();

		FFrameTime IncAutoStartTime = FFrameTime(MovieScene::DiscreteInclusiveLower(InnerMovieScene->GetPlaybackRange())) * InnerToOuter;
		FFrameTime ExcAutoEndTime   = FFrameTime(MovieScene::DiscreteExclusiveUpper(InnerMovieScene->GetPlaybackRange())) * InnerToOuter;

		return TRange<FFrameNumber>(GetInclusiveStartFrame(), GetInclusiveStartFrame() + (ExcAutoEndTime.RoundToFrame() - IncAutoStartTime.RoundToFrame()));
	}

	return Super::GetAutoSizeRange();
}

void UMovieSceneSubSection::TrimSection( FQualifiedFrameTime TrimTime, bool bTrimLeft, bool bDeleteKeys)
{
	TRange<FFrameNumber> InitialRange = GetRange();
	if ( !InitialRange.Contains( TrimTime.Time.GetFrame() ) )
	{
		return;
	}

	FFrameNumber InitialStartOffset = Parameters.StartFrameOffset;

	UMovieSceneSection::TrimSection( TrimTime, bTrimLeft, bDeleteKeys );

	// If trimming off the left, set the offset of the shot
	if ( bTrimLeft && InitialRange.GetLowerBound().IsClosed() )
	{
		// Sections need their offsets calculated in their local resolution. Different sequences can have different tick resolutions 
		// so we need to transform from the parent resolution to the local one before splitting them.
		FFrameRate LocalTickResolution = GetSequence()->GetMovieScene()->GetTickResolution();
		FFrameNumber LocalResolutionStartOffset = FFrameRate::TransformTime(TrimTime.Time.GetFrame() - MovieScene::DiscreteInclusiveLower(InitialRange), TrimTime.Rate, LocalTickResolution).FrameNumber;


		FFrameNumber NewStartOffset = LocalResolutionStartOffset * Parameters.TimeScale;
		NewStartOffset += InitialStartOffset;

		// Ensure start offset is not less than 0
		if (NewStartOffset >= 0)
		{
			Parameters.StartFrameOffset = NewStartOffset;
		}
	}
}

FMovieSceneSubSequenceData UMovieSceneSubSection::GenerateSubSequenceData(const FSubSequenceInstanceDataParams& Params) const
{
	return FMovieSceneSubSequenceData(*this);
}

FFrameNumber UMovieSceneSubSection::MapTimeToSectionFrame(FFrameTime InPosition) const
{
	FFrameNumber LocalPosition = ((InPosition - Parameters.StartFrameOffset) * Parameters.TimeScale).GetFrame();
	return LocalPosition;
}
