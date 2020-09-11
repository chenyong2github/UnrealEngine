// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Evaluation/MovieSceneEvaluationCustomVersion.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Evaluation/MovieSceneSkeletalAnimationTemplate.h"
#include "Compilation/IMovieSceneTemplateGenerator.h"
#include "Compilation/MovieSceneEvaluationTreePopulationRules.h"
#include "MovieScene.h"

#define LOCTEXT_NAMESPACE "MovieSceneSkeletalAnimationTrack"


/* UMovieSceneSkeletalAnimationTrack structors
 *****************************************************************************/

UMovieSceneSkeletalAnimationTrack::UMovieSceneSkeletalAnimationTrack(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bUseLegacySectionIndexBlend(false)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(124, 15, 124, 65);
	bSupportsDefaultSections = false;
#endif

	SupportedBlendTypes.Add(EMovieSceneBlendType::Absolute);

	EvalOptions.bEvaluateNearestSection_DEPRECATED = EvalOptions.bCanEvaluateNearestSection = true;
}


/* UMovieSceneSkeletalAnimationTrack interface
 *****************************************************************************/

FMovieSceneEvalTemplatePtr UMovieSceneSkeletalAnimationTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneSkeletalAnimationSectionTemplate(*CastChecked<const UMovieSceneSkeletalAnimationSection>(&InSection));
}

UMovieSceneSection* UMovieSceneSkeletalAnimationTrack::AddNewAnimationOnRow(FFrameNumber KeyTime, UAnimSequenceBase* AnimSequence, int32 RowIndex)
{
	UMovieSceneSkeletalAnimationSection* NewSection = Cast<UMovieSceneSkeletalAnimationSection>(CreateNewSection());
	{
		FFrameTime AnimationLength = AnimSequence->GetPlayLength() * GetTypedOuter<UMovieScene>()->GetTickResolution();
		int32 IFrameNumber = AnimationLength.FrameNumber.Value + (int)(AnimationLength.GetSubFrame() + 0.5f) + 1;
		NewSection->InitialPlacementOnRow(AnimationSections, KeyTime, IFrameNumber, RowIndex);
		NewSection->Params.Animation = AnimSequence;
	}

	AddSection(*NewSection);

	return NewSection;
}


TArray<UMovieSceneSection*> UMovieSceneSkeletalAnimationTrack::GetAnimSectionsAtTime(FFrameNumber Time)
{
	TArray<UMovieSceneSection*> Sections;
	for (auto Section : AnimationSections)
	{
		if (Section->IsTimeWithinSection(Time))
		{
			Sections.Add(Section);
		}
	}

	return Sections;
}


/* UMovieSceneTrack interface
 *****************************************************************************/

void UMovieSceneSkeletalAnimationTrack::PostLoad()
{
	Super::PostLoad();

	if (GetLinkerCustomVersion(FMovieSceneEvaluationCustomVersion::GUID) < FMovieSceneEvaluationCustomVersion::AddBlendingSupport)
	{
		bUseLegacySectionIndexBlend = true;
	}
}

const TArray<UMovieSceneSection*>& UMovieSceneSkeletalAnimationTrack::GetAllSections() const
{
	return AnimationSections;
}


bool UMovieSceneSkeletalAnimationTrack::SupportsMultipleRows() const
{
	return true;
}

bool UMovieSceneSkeletalAnimationTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneSkeletalAnimationSection::StaticClass();
}

UMovieSceneSection* UMovieSceneSkeletalAnimationTrack::CreateNewSection()
{
	return NewObject<UMovieSceneSkeletalAnimationSection>(this, NAME_None, RF_Transactional);
}


void UMovieSceneSkeletalAnimationTrack::RemoveAllAnimationData()
{
	AnimationSections.Empty();
}


bool UMovieSceneSkeletalAnimationTrack::HasSection(const UMovieSceneSection& Section) const
{
	return AnimationSections.Contains(&Section);
}


void UMovieSceneSkeletalAnimationTrack::AddSection(UMovieSceneSection& Section)
{
	AnimationSections.Add(&Section);
}


void UMovieSceneSkeletalAnimationTrack::RemoveSection(UMovieSceneSection& Section)
{
	AnimationSections.Remove(&Section);
}


void UMovieSceneSkeletalAnimationTrack::RemoveSectionAt(int32 SectionIndex)
{
	AnimationSections.RemoveAt(SectionIndex);
}


bool UMovieSceneSkeletalAnimationTrack::IsEmpty() const
{
	return AnimationSections.Num() == 0;
}

#if WITH_EDITORONLY_DATA

FText UMovieSceneSkeletalAnimationTrack::GetDefaultDisplayName() const
{
	return LOCTEXT("TrackName", "Animation");
}

#endif

bool UMovieSceneSkeletalAnimationTrack::PopulateEvaluationTree(TMovieSceneEvaluationTree<FMovieSceneTrackEvaluationData>& OutData) const
{
	using namespace UE::MovieScene;

	if (!bUseLegacySectionIndexBlend)
	{
		FEvaluationTreePopulationRules::HighPassPerRow(AnimationSections, OutData);
	}
	else
	{
		// Use legacy blending... when there's overlapping, the section that makes it into the evaluation tree is
		// the one that appears later in the container arary of section data.
		//
		auto SortByLatestInArrayAndRow = [](const FEvaluationTreePopulationRules::FSortedSection& A, const FEvaluationTreePopulationRules::FSortedSection& B)
		{
			if (A.Row() == B.Row())
			{
				return A.Index > B.Index;
			}
			
			return A.Row() < B.Row();
		};

		UE::MovieScene::FEvaluationTreePopulationRules::HighPassCustomPerRow(AnimationSections, OutData, SortByLatestInArrayAndRow);
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
