// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieSceneEventTrackExtensions.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Sections/MovieSceneEventRepeaterSection.h"
#include "Sections/MovieSceneEventTriggerSection.h"

UMovieSceneEventRepeaterSection* UMovieSceneEventTrackExtensions::AddEventRepeaterSection(UMovieSceneEventTrack* Track)
{
	UMovieSceneEventRepeaterSection* NewSection = NewObject<UMovieSceneEventRepeaterSection>(Track, NAME_None, RF_Transactional);

	if (NewSection)
	{
		Track->Modify();

		Track->AddSection(*NewSection);
	}

	return NewSection;
}

UMovieSceneEventTriggerSection* UMovieSceneEventTrackExtensions::AddEventTriggerSection(UMovieSceneEventTrack* Track)
{
	UMovieSceneEventTriggerSection* NewSection = NewObject<UMovieSceneEventTriggerSection>(Track, NAME_None, RF_Transactional);

	if (NewSection)
	{
		Track->Modify();

		Track->AddSection(*NewSection);
	}

	return NewSection;
}
