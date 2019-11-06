// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineConfigBase.h"

#define LOCTEXT_NAMESPACE "MoviePipelineConfigBase"

void UMoviePipelineConfigBase::RemoveSetting(UMoviePipelineSetting* InSource)
{
	Settings.Remove(InSource);

	// Update our cached serial number so the UI rebuilds the tree
	++SettingsSerialNumber;
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineConfigBase"