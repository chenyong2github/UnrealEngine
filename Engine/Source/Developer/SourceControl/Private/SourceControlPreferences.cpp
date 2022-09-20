// Copyright Epic Games, Inc. All Rights Reserved.

#include "SourceControlPreferences.h"

bool USourceControlPreferences::IsValidationTagEnabled()
{
	return GetDefault<USourceControlPreferences>()->bEnableValidationTag;
}

bool USourceControlPreferences::ShouldDeleteNewFilesOnRevert()
{
	return GetDefault<USourceControlPreferences>()->bShouldDeleteNewFilesOnRevert;
}

bool USourceControlPreferences::AreUncontrolledChangelistsEnabled()
{
	return GetDefault<USourceControlPreferences>()->bEnableUncontrolledChangelists;
}
