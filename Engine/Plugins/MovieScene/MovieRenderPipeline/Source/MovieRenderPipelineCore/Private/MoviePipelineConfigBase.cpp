// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineConfigBase.h"
#include "MoviePipelineSetting.h"

#define LOCTEXT_NAMESPACE "MoviePipelineConfigBase"

void UMoviePipelineConfigBase::RemoveSetting(UMoviePipelineSetting* InSource)
{
	Settings.Remove(InSource);

	// Update our cached serial number so the UI rebuilds the tree
	++SettingsSerialNumber;
}

void UMoviePipelineConfigBase::CopyFrom(UMoviePipelineConfigBase* InConfig)
{
#if WITH_EDITOR
	Modify();
#endif

	Settings.Empty();

	// Only access the direct Settings array
	for (UMoviePipelineSetting* Setting : InConfig->Settings)
	{
		UMoviePipelineSetting* Duplicate = Cast<UMoviePipelineSetting>(StaticDuplicateObject(Setting, this, Setting->GetFName()));
		Settings.Add(Duplicate);
	}

	// Manually bump this since we directly added to the Settings array
	SettingsSerialNumber++;
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineConfigBase"