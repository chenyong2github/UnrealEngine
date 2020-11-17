// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sections/MovieSceneCinematicShotSection.h"


/* UMovieSceneCinematicshotSection structors
 *****************************************************************************/

UMovieSceneCinematicShotSection::UMovieSceneCinematicShotSection(const FObjectInitializer& ObjInitializer)
	: Super(ObjInitializer)
{ }

void UMovieSceneCinematicShotSection::PostLoad()
{
	Super::PostLoad();

	if (!DisplayName_DEPRECATED.IsEmpty())
	{
		ShotDisplayName = DisplayName_DEPRECATED.ToString();
		DisplayName_DEPRECATED = FText::GetEmpty();
	}
}
