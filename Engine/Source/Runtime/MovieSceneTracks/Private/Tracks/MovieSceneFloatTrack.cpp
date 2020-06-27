// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracks/MovieSceneFloatTrack.h"
#include "MovieSceneCommonHelpers.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Evaluation/MovieScenePropertyTemplates.h"


UMovieSceneFloatTrack::UMovieSceneFloatTrack( const FObjectInitializer& ObjectInitializer )
	: Super( ObjectInitializer )
{
	SupportedBlendTypes = FMovieSceneBlendTypeField::All();
}

bool UMovieSceneFloatTrack::SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const
{
	return SectionClass == UMovieSceneFloatSection::StaticClass();
}

UMovieSceneSection* UMovieSceneFloatTrack::CreateNewSection()
{
	return NewObject<UMovieSceneFloatSection>(this, NAME_None, RF_Transactional);
}

