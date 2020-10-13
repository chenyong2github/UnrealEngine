// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "MovieScene.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "IMovieSceneTracksModule.h"

#define LOCTEXT_NAMESPACE "MovieSceneCameraCutTrack"

/* UMovieSceneCameraCutTrack interface
 *****************************************************************************/
UMovieSceneCameraCutTrack::UMovieSceneCameraCutTrack( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
	, bCanBlend(false)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(120, 120, 120, 65);
#endif

	// By default, don't evaluate camera cuts in pre and postroll
	EvalOptions.bEvaluateInPreroll = EvalOptions.bEvaluateInPostroll = false;

	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);
}

UMovieSceneCameraCutSection* UMovieSceneCameraCutTrack::AddNewCameraCut(const FMovieSceneObjectBindingID& CameraBindingID, FFrameNumber StartTime)
{
	Modify();

	FFrameNumber NewSectionEndTime = FindEndTimeForCameraCut(StartTime);

	// If there's an existing section, just swap the camera guid
	UMovieSceneCameraCutSection* ExistingSection = nullptr;
	for (auto Section : Sections)
	{
		if (Section->HasStartFrame() && Section->HasEndFrame() && Section->GetInclusiveStartFrame() == StartTime && Section->GetExclusiveEndFrame() == NewSectionEndTime)
		{
			ExistingSection = Cast<UMovieSceneCameraCutSection>(Section);
			break;
		}
	}

	UMovieSceneCameraCutSection* NewSection = ExistingSection;
	if (ExistingSection != nullptr)
	{
		ExistingSection->SetCameraBindingID(CameraBindingID);
	}
	else
	{
		NewSection = NewObject<UMovieSceneCameraCutSection>(this, NAME_None, RF_Transactional);
		NewSection->SetRange(TRange<FFrameNumber>(StartTime, NewSectionEndTime));
		NewSection->SetCameraBindingID(CameraBindingID);

		AddSection(*NewSection);
	}

	// When a new CameraCut is added, sort all CameraCuts to ensure they are in the correct order
	MovieSceneHelpers::SortConsecutiveSections(Sections);

	// Once CameraCuts are sorted fixup the surrounding CameraCuts to fix any gaps
	if (bCanBlend)
	{
		MovieSceneHelpers::FixupConsecutiveBlendingSections(Sections, *NewSection, false);
	}
	else
	{
		MovieSceneHelpers::FixupConsecutiveSections(Sections, *NewSection, false);
	}

	return NewSection;
}


/* UMovieSceneTrack interface
 *****************************************************************************/

void UMovieSceneCameraCutTrack::AddSection(UMovieSceneSection& Section)
{
	if (UMovieSceneCameraCutSection* CutSection = Cast<UMovieSceneCameraCutSection>(&Section))
	{
		Sections.Add(CutSection);
	}
}

bool UMovieSceneCameraCutTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneCameraCutSection::StaticClass();
}

UMovieSceneSection* UMovieSceneCameraCutTrack::CreateNewSection()
{
	return NewObject<UMovieSceneCameraCutSection>(this, NAME_None, RF_Transactional);
}

bool UMovieSceneCameraCutTrack::SupportsMultipleRows() const
{
	return false;
}

EMovieSceneTrackEasingSupportFlags UMovieSceneCameraCutTrack::SupportsEasing(FMovieSceneSupportsEasingParams& Params) const
{
	if (!bCanBlend)
	{
		return EMovieSceneTrackEasingSupportFlags::None;
	}
	if (Params.ForSection != nullptr)
	{
		const int32 NumSections = Sections.Num();
		if (NumSections == 1)
		{
			return EMovieSceneTrackEasingSupportFlags::AutomaticEasing | EMovieSceneTrackEasingSupportFlags::ManualEasing;
		}
		else if (NumSections > 1)
		{
			if (Params.ForSection == Sections[0])
			{
				return EMovieSceneTrackEasingSupportFlags::AutomaticEasing | EMovieSceneTrackEasingSupportFlags::ManualEaseIn;
			}
			if (Params.ForSection == Sections.Last())
			{
				return EMovieSceneTrackEasingSupportFlags::AutomaticEasing | EMovieSceneTrackEasingSupportFlags::ManualEaseOut;
			}
		}
	}
	return EMovieSceneTrackEasingSupportFlags::AutomaticEasing;
}

const TArray<UMovieSceneSection*>& UMovieSceneCameraCutTrack::GetAllSections() const
{
	return Sections;
}

void UMovieSceneCameraCutTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);

	if (bCanBlend)
	{
		MovieSceneHelpers::FixupConsecutiveSections(Sections, Section, true);
	}
	else
	{
		MovieSceneHelpers::FixupConsecutiveBlendingSections(Sections, Section, true);
	}

	// @todo Sequencer: The movie scene owned by the section is now abandoned.  Should we offer to delete it?  
}

void UMovieSceneCameraCutTrack::RemoveSectionAt(int32 SectionIndex)
{
	UMovieSceneSection* SectionToDelete = Sections[SectionIndex];
	if (bCanBlend)
	{
		MovieSceneHelpers::FixupConsecutiveSections(Sections, *SectionToDelete, true);
	}
	else
	{
		MovieSceneHelpers::FixupConsecutiveBlendingSections(Sections, *SectionToDelete, true);
	}

	Sections.RemoveAt(SectionIndex);
	MovieSceneHelpers::SortConsecutiveSections(Sections);
}

void UMovieSceneCameraCutTrack::RemoveAllAnimationData()
{
	Sections.Empty();
}

#if WITH_EDITORONLY_DATA
FText UMovieSceneCameraCutTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Camera Cuts");
}
#endif


#if WITH_EDITOR
void UMovieSceneCameraCutTrack::OnSectionMoved(UMovieSceneSection& Section, const FMovieSceneSectionMovedParams& Params)
{
	if (bCanBlend)
	{
		MovieSceneHelpers::FixupConsecutiveBlendingSections(Sections, Section, false);
	}
	else
	{
		MovieSceneHelpers::FixupConsecutiveSections(Sections, Section, false);
	}
}
#endif

FFrameNumber UMovieSceneCameraCutTrack::FindEndTimeForCameraCut( FFrameNumber StartTime )
{
	UMovieScene* OwnerScene = GetTypedOuter<UMovieScene>();

	// End time should default to end where the movie scene ends. Ensure it is at least the same as start time (this should only happen when the movie scene has an initial time range smaller than the start time)
	FFrameNumber ExclusivePlayEnd = UE::MovieScene::DiscreteExclusiveUpper(OwnerScene->GetPlaybackRange());
	FFrameNumber ExclusiveEndTime = FMath::Max( ExclusivePlayEnd, StartTime );

	for( UMovieSceneSection* Section : Sections )
	{
		if( Section->HasStartFrame() && Section->GetInclusiveStartFrame() > StartTime )
		{
			ExclusiveEndTime = Section->GetInclusiveStartFrame();
			break;
		}
	}

	if( StartTime == ExclusiveEndTime )
	{
		// Give the CameraCut a reasonable length of time to start out with.  A 0 time CameraCut is not usable
		ExclusiveEndTime = (StartTime + .5f * OwnerScene->GetTickResolution()).FrameNumber;
	}

	return ExclusiveEndTime;
}

void UMovieSceneCameraCutTrack::PreCompileImpl()
{
	for (UMovieSceneSection* Section : Sections)
	{
		if (UMovieSceneCameraCutSection* CameraCutSection = CastChecked<UMovieSceneCameraCutSection>(Section))
		{
			CameraCutSection->ComputeInitialCameraCutTransform();
		}
	}
}

#undef LOCTEXT_NAMESPACE
