// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Tracks/TemplateSequenceTrack.h"
#include "IMovieSceneTracksModule.h"
#include "TemplateSequence.h"
#include "MovieSceneTimeHelpers.h"
#include "Sections/TemplateSequenceSection.h"
#include "Compilation/IMovieSceneTemplateGenerator.h"
#include "Evaluation/TemplateSequenceSectionTemplate.h"

#define LOCTEXT_NAMESPACE "TemplateSequenceTrack"

bool UTemplateSequenceTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UTemplateSequenceSection::StaticClass();
}

UMovieSceneSection* UTemplateSequenceTrack::CreateNewSection()
{
	return NewObject<UTemplateSequenceSection>(this, NAME_None, RF_Transactional);
}

UMovieSceneSection* UTemplateSequenceTrack::AddNewTemplateSequenceSection(FFrameNumber KeyTime, UTemplateSequence* InSequence)
{
	UTemplateSequenceSection* NewSection = Cast<UTemplateSequenceSection>(CreateNewSection());
	{
		UMovieScene* OuterMovieScene = GetTypedOuter<UMovieScene>();
		UMovieScene* InnerMovieScene = InSequence->GetMovieScene();

		int32      InnerSequenceLength = MovieScene::DiscreteSize(InnerMovieScene->GetPlaybackRange());
		FFrameTime OuterSequenceLength = ConvertFrameTime(InnerSequenceLength, InnerMovieScene->GetTickResolution(), OuterMovieScene->GetTickResolution());

		NewSection->InitialPlacement(Sections, KeyTime, OuterSequenceLength.FrameNumber.Value, SupportsMultipleRows());
		NewSection->SetSequence(InSequence);
	}

	AddSection(*NewSection);

	return NewSection;
}

void UTemplateSequenceTrack::PostCompile(FMovieSceneEvaluationTrack& OutTrack, const FMovieSceneTrackCompilerArgs& Args) const
{
	// Make sure out evaluation template runs before the spawn tracks because it will have to setup the overrides.
	OutTrack.SetEvaluationGroup(IMovieSceneTracksModule::GetEvaluationGroupName(EBuiltInEvaluationGroup::SpawnObjects));
	OutTrack.SetEvaluationPriority(GetEvaluationPriority());
}

#if WITH_EDITORONLY_DATA

FText UTemplateSequenceTrack::GetDisplayName() const
{
	return LOCTEXT("TrackName", "Template Animation");
}

#endif

#undef LOCTEXT_NAMESPACE
