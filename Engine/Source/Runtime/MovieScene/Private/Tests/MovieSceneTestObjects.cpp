// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTestObjects.h"
#include "Compilation/MovieSceneCompilerRules.h"
#include "Compilation/MovieSceneSegmentCompiler.h"

FMovieSceneEvalTemplatePtr UTestMovieSceneTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FTestMovieSceneEvalTemplate();
}

void UTestMovieSceneTrack::AddSection(UMovieSceneSection& Section)
{
	if (UTestMovieSceneSection* TestSection = Cast<UTestMovieSceneSection>(&Section))
	{
		SectionArray.Add(TestSection);
	}
}

bool UTestMovieSceneTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UTestMovieSceneSection::StaticClass();
}

UMovieSceneSection* UTestMovieSceneTrack::CreateNewSection()
{
	return NewObject<UTestMovieSceneSection>(this, NAME_None, RF_Transactional);
}


bool UTestMovieSceneTrack::HasSection(const UMovieSceneSection& Section) const
{
	return SectionArray.Contains(&Section);
}


bool UTestMovieSceneTrack::IsEmpty() const
{
	return SectionArray.Num() == 0;
}

void UTestMovieSceneTrack::RemoveSection(UMovieSceneSection& Section)
{
	SectionArray.Remove(&Section);
}

void UTestMovieSceneTrack::RemoveSectionAt(int32 SectionIndex)
{
	SectionArray.RemoveAt(SectionIndex);
}
