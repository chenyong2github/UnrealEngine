// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineConfigBase.h"
#include "MoviePipelineSetting.h"

#define LOCTEXT_NAMESPACE "MoviePipelineConfigBase"

void UMoviePipelineConfigBase::RemoveSetting(UMoviePipelineSetting* InSetting)
{
	Settings.Remove(InSetting);
	OnSettingRemoved(InSetting);

	// Update our cached serial number so the UI rebuilds the tree
	++SettingsSerialNumber;
}

void UMoviePipelineConfigBase::CopyFrom(UMoviePipelineConfigBase* InConfig)
{
#if WITH_EDITOR
	Modify();
#endif

	// Copy the display name from the other config. When we copy from a preset, the preset will
	// have a display name matching the asset name so it will look like you're using that preset.
	DisplayName = InConfig->DisplayName;

	Settings.Empty();

	// Only access the direct Settings array
	for (UMoviePipelineSetting* Setting : InConfig->Settings)
	{
		UMoviePipelineSetting* Duplicate = Cast<UMoviePipelineSetting>(StaticDuplicateObject(Setting, this, Setting->GetFName()));
		Duplicate->ValidateState();

		Settings.Add(Duplicate);
		OnSettingAdded(Duplicate);
	}

	// Manually bump this since we directly added to the Settings array
	SettingsSerialNumber++;
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineConfigBase"