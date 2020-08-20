// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneTakeSettings.h"

#define LOCTEXT_NAMESPACE "MovieSceneTakeSettings"

UMovieSceneTakeSettings::UMovieSceneTakeSettings()
	: HoursText(LOCTEXT("HoursText", "Hours"))
	, MinutesText(LOCTEXT("MinutesText", "Minutes"))
	, SecondsText(LOCTEXT("SecondsText", "Seconds"))
	, FramesText(LOCTEXT("FramesText", "Frames"))
	, SubFramesText(LOCTEXT("SubFramesText", "SubFrames"))
	, SlateText(LOCTEXT("Slatetext", "Slate"))
{
}

#if WITH_EDITOR
void UMovieSceneTakeSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		// Dragging spinboxes causes this to be called every frame so we wait until they've finished dragging before saving.
		SaveConfig();
	}
}
#endif

#undef LOCTEXT_NAMESPACE