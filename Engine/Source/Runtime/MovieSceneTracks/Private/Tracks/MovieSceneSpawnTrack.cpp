// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneSpawnTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneSpawnSection.h"
#include "Evaluation/MovieSceneSpawnTemplate.h"
#include "Evaluation/MovieSceneEvaluationTrack.h"
#include "Compilation/IMovieSceneTemplateGenerator.h"
#include "Serialization/ObjectReader.h"
#include "Serialization/ObjectWriter.h"
#include "MovieScene.h"
#include "IMovieSceneTracksModule.h"

#define LOCTEXT_NAMESPACE "MovieSceneSpawnTrack"


/* UMovieSceneSpawnTrack structors
 *****************************************************************************/

UMovieSceneSpawnTrack::UMovieSceneSpawnTrack(const FObjectInitializer& Obj)
	: Super(Obj)
{
#if WITH_EDITORONLY_DATA
	TrackTint = FColor(43, 43, 155, 65);
#endif
}

void UMovieSceneSpawnTrack::PostLoad()
{
	TArray<uint8> Bytes;

	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		UMovieSceneBoolSection* BoolSection = ExactCast<UMovieSceneBoolSection>(Sections[Index]);
		if (BoolSection)
		{
			BoolSection->ConditionalPostLoad();
			Bytes.Reset();

			FObjectWriter(BoolSection, Bytes);
			UMovieSceneSpawnSection* NewSection = NewObject<UMovieSceneSpawnSection>(this, NAME_None, RF_Transactional);
			FObjectReader(NewSection, Bytes);

			Sections[Index] = NewSection;
		}
	}

	Super::PostLoad();
}

/* UMovieSceneTrack interface
 *****************************************************************************/

bool UMovieSceneSpawnTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneSpawnSection::StaticClass();
}

UMovieSceneSection* UMovieSceneSpawnTrack::CreateNewSection()
{
	return NewObject<UMovieSceneSpawnSection>(this, NAME_None, RF_Transactional);
}


bool UMovieSceneSpawnTrack::HasSection(const UMovieSceneSection& Section) const
{
	return Sections.ContainsByPredicate([&](const UMovieSceneSection* In){ return In == &Section; });
}


void UMovieSceneSpawnTrack::AddSection(UMovieSceneSection& Section)
{
	Sections.Add(&Section);
}


void UMovieSceneSpawnTrack::RemoveSection(UMovieSceneSection& Section)
{
	Sections.RemoveAll([&](const UMovieSceneSection* In) { return In == &Section; });
}

void UMovieSceneSpawnTrack::RemoveSectionAt(int32 SectionIndex)
{
	Sections.RemoveAt(SectionIndex);
}

void UMovieSceneSpawnTrack::RemoveAllAnimationData()
{
	Sections.Empty();
}


bool UMovieSceneSpawnTrack::IsEmpty() const
{
	return Sections.Num() == 0;
}


const TArray<UMovieSceneSection*>& UMovieSceneSpawnTrack::GetAllSections() const
{
	return Sections;
}

FMovieSceneEvalTemplatePtr UMovieSceneSpawnTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	const UMovieSceneSpawnSection* BoolSection = CastChecked<const UMovieSceneSpawnSection>(&InSection);
	return FMovieSceneSpawnSectionTemplate(*BoolSection);
}

void UMovieSceneSpawnTrack::GenerateTemplate(const FMovieSceneTrackCompilerArgs& Args) const
{
	UMovieScene* ParentMovieScene = GetTypedOuter<UMovieScene>();
	if (ParentMovieScene && ParentMovieScene->FindPossessable(Args.ObjectBindingId))
	{
		return;
	}

	Super::GenerateTemplate(Args);
}

void UMovieSceneSpawnTrack::PostCompile(FMovieSceneEvaluationTrack& OutTrack, const FMovieSceneTrackCompilerArgs& Args) const
{
	// All objects must be spawned/destroyed before the sequence continues
	OutTrack.SetEvaluationGroup(IMovieSceneTracksModule::GetEvaluationGroupName(EBuiltInEvaluationGroup::SpawnObjects));
	// Set priority to highest possible
	OutTrack.SetEvaluationPriority(GetEvaluationPriority());

	OutTrack.PrioritizeTearDown();
}

#if WITH_EDITORONLY_DATA

FText UMovieSceneSpawnTrack::GetDisplayName() const
{
	return LOCTEXT("TrackName", "Spawned");
}

#endif


#undef LOCTEXT_NAMESPACE
