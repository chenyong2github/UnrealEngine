// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineColorSetting.h"

UMoviePipelineColorSetting::UMoviePipelineColorSetting() : bDisableToneCurve(false)
{
}

bool UMoviePipelineColorSetting::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetName() == GET_MEMBER_NAME_STRING_CHECKED(UMoviePipelineColorSetting, bDisableToneCurve))
	{
		return !OCIOConfiguration.bIsEnabled;
	}

	return Super::CanEditChange(InProperty);
}