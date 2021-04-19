// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlSettings.h"

bool USourceControlSettings::IsValidationTagEnabled()
{
	return GetDefault<USourceControlSettings>()->bEnableValidationTag;
}
