// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneMediaTrack.h"

#include "MediaPlayer.h"
#include "MediaSource.h"
#include "MovieScene.h"
#include "MovieSceneMediaSection.h"
#include "MovieSceneMediaTemplate.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MovieSceneMediaTrack)


#define LOCTEXT_NAMESPACE "MovieSceneMediaTrack"


/* UMovieSceneMediaTrack interface
 *****************************************************************************/

UMovieSceneMediaTrack::UMovieSceneMediaTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	EvalOptions.bCanEvaluateNearestSection = false;
	EvalOptions.bEvalNearestSection = false;
	EvalOptions.bEvaluateInPreroll = true;
	EvalOptions.bEvaluateInPostroll = true;

	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);

#if WITH_EDITORONLY_DATA
	TrackTint = FColor(0, 0, 0, 200);
	bSupportsDefaultSections = false;
#endif
}


/* UMovieSceneMediaTrack interface
 *****************************************************************************/

UMovieSceneSection* UMovieSceneMediaTrack::AddNewMediaSourceOnRow(UMediaSource& MediaSource, FFrameNumber Time, int32 RowIndex)
{
	UMovieSceneMediaSection* NewSection = AddNewSectionOnRow(Time, RowIndex);
	NewSection->SetMediaSource(&MediaSource);
	return NewSection;
}


UMovieSceneSection* UMovieSceneMediaTrack::AddNewMediaSourceProxyOnRow(const FMovieSceneObjectBindingID& ObjectBinding, int32 MediaSourceProxyIndex, FFrameNumber Time, int32 RowIndex)
{
	UMovieSceneMediaSection* NewSection = AddNewSectionOnRow(Time, RowIndex);
	NewSection->SetMediaSourceProxy(ObjectBinding, MediaSourceProxyIndex);
	return NewSection;
}


UMovieSceneMediaSection* UMovieSceneMediaTrack::AddNewSectionOnRow(FFrameNumber Time, int32 RowIndex)
{
	const float DefaultMediaSectionDuration = 1.0f;
	FFrameRate TickResolution = GetTypedOuter<UMovieScene>()->GetTickResolution();
	FFrameTime DurationToUse  = DefaultMediaSectionDuration * TickResolution;

	// add the section
	UMovieSceneMediaSection* NewSection = NewObject<UMovieSceneMediaSection>(this, NAME_None, RF_Transactional);

	NewSection->InitialPlacementOnRow(MediaSections, Time, DurationToUse.FrameNumber.Value, RowIndex);

	MediaSections.Add(NewSection);
	UpdateSectionTextureIndices();

	return NewSection;
}


/* UMovieScenePropertyTrack interface
 *****************************************************************************/

void UMovieSceneMediaTrack::AddSection(UMovieSceneSection& Section)
{
	MediaSections.Add(&Section);
	UpdateSectionTextureIndices();
}


bool UMovieSceneMediaTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneMediaSection::StaticClass();
}


UMovieSceneSection* UMovieSceneMediaTrack::CreateNewSection()
{
	return NewObject<UMovieSceneMediaSection>(this, NAME_None, RF_Transactional);
}


const TArray<UMovieSceneSection*>& UMovieSceneMediaTrack::GetAllSections() const
{
	return MediaSections;
}


bool UMovieSceneMediaTrack::HasSection(const UMovieSceneSection& Section) const
{
	return MediaSections.Contains(&Section);
}


bool UMovieSceneMediaTrack::IsEmpty() const
{
	return MediaSections.Num() == 0;
}


void UMovieSceneMediaTrack::RemoveSection(UMovieSceneSection& Section)
{
	MediaSections.Remove(&Section);
	UpdateSectionTextureIndices();
}

void UMovieSceneMediaTrack::RemoveSectionAt(int32 SectionIndex)
{
	MediaSections.RemoveAt(SectionIndex);
	UpdateSectionTextureIndices();
}

#if WITH_EDITOR

EMovieSceneSectionMovedResult UMovieSceneMediaTrack::OnSectionMoved(UMovieSceneSection& InSection, const FMovieSceneSectionMovedParams& Params)
{
	UpdateSectionTextureIndices();
	return EMovieSceneSectionMovedResult::None;
}

#endif // WITH_EDITOR

FMovieSceneEvalTemplatePtr UMovieSceneMediaTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneMediaSectionTemplate(*CastChecked<const UMovieSceneMediaSection>(&InSection), *this);
}

void UMovieSceneMediaTrack::UpdateSectionTextureIndices()
{
#if WITH_EDITOR
	// Do we have an object binding?
	UMovieScene* MovieScene = Cast<UMovieScene>(GetOuter());
	if (MovieScene != nullptr)
	{
		const TArray<FMovieSceneBinding>& Bindings = MovieScene->GetBindings();
		for (const FMovieSceneBinding& Binding : Bindings)
		{
			bool bIsThisMyBinding = false;
			const TArray<UMovieSceneTrack*>& Tracks = Binding.GetTracks();
			for (UMovieSceneTrack* Track : Tracks)
			{
				if ((Track != nullptr) && (Track == this))
				{
					bIsThisMyBinding = true;
					break;
				}
			}

			if (bIsThisMyBinding)
			{
				// Get all sections.
				TArray<UMovieSceneMediaSection*> AllSections;
				for (UMovieSceneTrack* Track : Tracks)
				{
					if (Track != nullptr)
					{
						const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
						for (UMovieSceneSection* Section : Sections)
						{
							UMovieSceneMediaSection* MediaSection = Cast<UMovieSceneMediaSection>(Section);
							if (MediaSection != nullptr)
							{
								AllSections.Add(MediaSection);
							}
						}
					}
				}

				// Set up indices from earliest section to latest.
				AllSections.Sort([](UMovieSceneMediaSection& A, UMovieSceneMediaSection& B)
				{ return A.GetRange().GetLowerBoundValue() < B.GetRange().GetLowerBoundValue(); });

				int32 CurrentTextureIndex = 0;
				UMovieSceneMediaSection* PreviousSection = nullptr;
				TRange<FFrameNumber> PreviousRange;
				for (UMovieSceneMediaSection* Section : AllSections)
				{
					TRange <FFrameNumber> Range = Section->GetRange();
					
					// If we overlap the previous section then we need another texture so increment
					// the count. Otherwise we can reuse the same index as the previous section.
					bool bIsOverlappingPreviousSection = false;
					if (PreviousRange.HasUpperBound())
					{
						bIsOverlappingPreviousSection = Range.GetLowerBoundValue() <
							PreviousRange.GetUpperBoundValue();
					}
					if (bIsOverlappingPreviousSection)
					{
						CurrentTextureIndex = (CurrentTextureIndex + 1);
					}
					else if (PreviousSection != nullptr)
					{
						CurrentTextureIndex = PreviousSection->TextureIndex;
					}

					Section->TextureIndex = CurrentTextureIndex;

					// Previous section is defined as the section right before in the timeline, which
					// is not necessarily the last section we looked at.
					if ((PreviousRange.HasUpperBound() == false) ||
						(PreviousRange.GetUpperBoundValue() < Range.GetUpperBoundValue()))
					{
						PreviousSection = Section;
						PreviousRange = Range;
					}
				}
				break;
			}
		}
	}
#endif // WITH_EDITOR
}

#undef LOCTEXT_NAMESPACE

