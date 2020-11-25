// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneObjectPropertyTrack.h"
#include "Sections/MovieSceneObjectPropertySection.h"
#include "Evaluation/MovieSceneObjectPropertyTemplate.h"


UMovieSceneObjectPropertyTrack::UMovieSceneObjectPropertyTrack(const FObjectInitializer& ObjInit)
	: Super(ObjInit)
{
	PropertyClass = nullptr;
}

bool UMovieSceneObjectPropertyTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneObjectPropertySection::StaticClass();
}


UMovieSceneSection* UMovieSceneObjectPropertyTrack::CreateNewSection()
{
	UMovieSceneObjectPropertySection* Section = NewObject<UMovieSceneObjectPropertySection>(this, NAME_None, RF_Transactional);
	Section->ObjectChannel.SetPropertyClass(PropertyClass);
	return Section;
}

FMovieSceneEvalTemplatePtr UMovieSceneObjectPropertyTrack::CreateTemplateForSection(const UMovieSceneSection& InSection) const
{
	return FMovieSceneObjectPropertyTemplate(*CastChecked<UMovieSceneObjectPropertySection>(&InSection), *this);
}
