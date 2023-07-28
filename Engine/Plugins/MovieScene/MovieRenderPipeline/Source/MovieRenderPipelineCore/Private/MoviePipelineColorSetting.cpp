// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineColorSetting.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MoviePipelineColorSetting)

UMoviePipelineColorSetting::UMoviePipelineColorSetting() : bDisableToneCurve(false)
{
	// Note: Since MRQ settings already have enabled/disabled state control, this is slighty redundant.
	// As such, we retain the old behavior and always default to enabled == true.
	OCIOConfiguration.bIsEnabled = true;
}

#if WITH_EDITOR
bool UMoviePipelineColorSetting::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(UMoviePipelineColorSetting, bDisableToneCurve))
	{
		return !OCIOConfiguration.bIsEnabled;
	}

	return Super::CanEditChange(InProperty);
}
#endif // #if WITH_EDITOR
