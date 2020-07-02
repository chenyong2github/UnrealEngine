// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "MovieSceneSequence.h"
#include "MovieSceneCommonHelpers.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Compilation/MovieSceneCompilerRules.h"


#define LOCTEXT_NAMESPACE "MovieSceneCinematicShotTrack"


/* UMovieSceneSubTrack interface
 *****************************************************************************/
UMovieSceneCinematicShotTrack::UMovieSceneCinematicShotTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(0, 0, 0, 127);
#endif
}

UMovieSceneSubSection* UMovieSceneCinematicShotTrack::AddSequenceOnRow(UMovieSceneSequence* Sequence, FFrameNumber StartTime, int32 Duration, int32 RowIndex)
{
	UMovieSceneSubSection* NewSection = UMovieSceneSubTrack::AddSequenceOnRow(Sequence, StartTime, Duration, RowIndex);

	UMovieSceneCinematicShotSection* NewShotSection = Cast<UMovieSceneCinematicShotSection>(NewSection);

#if WITH_EDITOR

	if (Sequence != nullptr)
	{
		NewShotSection->SetShotDisplayName(Sequence->GetDisplayName().ToString());	
	}

#endif

	// When a new sequence is added, sort all sequences to ensure they are in the correct order
	MovieSceneHelpers::SortConsecutiveSections(Sections);

	// Once sequences are sorted fixup the surrounding sequences to fix any gaps
	//MovieSceneHelpers::FixupConsecutiveSections(Sections, *NewSection, false);

	return NewSection;
}

/* UMovieSceneTrack interface
 *****************************************************************************/

void UMovieSceneCinematicShotTrack::AddSection(UMovieSceneSection& Section)
{
	if (Section.IsA<UMovieSceneCinematicShotSection>())
	{
		Sections.Add(&Section);
	}
}

bool UMovieSceneCinematicShotTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneCinematicShotSection::StaticClass();
}


UMovieSceneSection* UMovieSceneCinematicShotTrack::CreateNewSection()
{
	return NewObject<UMovieSceneCinematicShotSection>(this, NAME_None, RF_Transactional);
}

void UMovieSceneCinematicShotTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.Remove(&Section);
	//MovieSceneHelpers::FixupConsecutiveSections(Sections, Section, true);
	MovieSceneHelpers::SortConsecutiveSections(Sections);

	// @todo Sequencer: The movie scene owned by the section is now abandoned.  Should we offer to delete it?  
}

void UMovieSceneCinematicShotTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
	MovieSceneHelpers::SortConsecutiveSections(Sections);
}

bool UMovieSceneCinematicShotTrack::SupportsMultipleRows() const
{
	return true;
}

namespace UE
{
namespace MovieScene
{
	enum class ECinematicShotSectionSortFlags
	{
		None = 0,
		PreRoll = 1 << 0,
		PostRoll = 1 << 1
	};
	ENUM_CLASS_FLAGS(ECinematicShotSectionSortFlags);

	struct FCinematicShotSectionSortData
	{
		int32 Row;
		int32 OverlapPriority;
		int32 SectionIndex;
		TRangeBound<FFrameNumber> LowerBound;
		ECinematicShotSectionSortFlags Flags = ECinematicShotSectionSortFlags::None;

		friend bool operator<(const FCinematicShotSectionSortData& A, const FCinematicShotSectionSortData& B)
		{
			const bool PrePostRollA = EnumHasAnyFlags(A.Flags, ECinematicShotSectionSortFlags::PreRoll | ECinematicShotSectionSortFlags::PostRoll);
			const bool PrePostRollB = EnumHasAnyFlags(B.Flags, ECinematicShotSectionSortFlags::PreRoll | ECinematicShotSectionSortFlags::PostRoll);

			if (PrePostRollA != PrePostRollB)
			{
				return PrePostRollA;
			}
			else if (PrePostRollA)
			{
				return false;
			}
			else if (A.OverlapPriority == B.OverlapPriority)
			{
				return TRangeBound<FFrameNumber>::MaxLower(A.LowerBound, B.LowerBound) == A.LowerBound;
			}
			return A.OverlapPriority > B.OverlapPriority;
		}
	};
}
}

bool UMovieSceneCinematicShotTrack::PopulateEvaluationTree(TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData) const
{
	using namespace UE::MovieScene;

	TArray<FCinematicShotSectionSortData, TInlineAllocator<16>> SortedSections;

	for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
	{
		UMovieSceneSection* Section = Sections[SectionIndex];

		if (Section && Section->IsActive())
		{
			const TRange<FFrameNumber> SectionRange = Section->GetRange();
			if (!SectionRange.IsEmpty())
			{
				FCinematicShotSectionSortData SectionData{ 
					Section->GetRowIndex(), Section->GetOverlapPriority(), SectionIndex, Section->GetRange().GetLowerBound() };
				if (!SectionRange.GetLowerBound().IsOpen() && Section->GetPreRollFrames() > 0)
				{
					SectionData.Flags |= ECinematicShotSectionSortFlags::PreRoll;
				}
				if (!SectionRange.GetUpperBound().IsOpen() && Section->GetPostRollFrames() > 0)
				{
					SectionData.Flags |= ECinematicShotSectionSortFlags::PostRoll;
				}
				SortedSections.Add(SectionData);
			}
		}
	}

	SortedSections.Sort();

	for (const FCinematicShotSectionSortData& SectionData : SortedSections)
	{
		UMovieSceneSection* Section = Sections[SectionData.SectionIndex];
		OutData.AddIfEmpty(Section->GetRange(), FMovieSceneTrackEvaluationData::FromSection(Section));
	}

	return true;
}

int8 UMovieSceneCinematicShotTrack::GetEvaluationFieldVersion() const
{
	return 1;
}

#if WITH_EDITOR
void UMovieSceneCinematicShotTrack::OnSectionMoved(UMovieSceneSection& Section, const FMovieSceneSectionMovedParams& Params)
{
	//MovieSceneHelpers::FixupConsecutiveSections(Sections, Section, false);
}
#endif

#if WITH_EDITORONLY_DATA
FText UMovieSceneCinematicShotTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Shots");
}
#endif

void UMovieSceneCinematicShotTrack::SortSections()
{
	MovieSceneHelpers::SortConsecutiveSections(Sections);
}

#undef LOCTEXT_NAMESPACE
