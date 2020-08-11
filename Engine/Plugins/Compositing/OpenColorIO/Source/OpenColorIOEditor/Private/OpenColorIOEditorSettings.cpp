// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenColorIOEditorSettings.h"


const FOpenColorIODisplayConfiguration* UOpenColorIOLevelViewportSettings::GetViewportSettings(FName ViewportIdentifier) const
{
	const FPerViewportDisplaySettingPair* Pair = ViewportsSettings.FindByPredicate([ViewportIdentifier](const FPerViewportDisplaySettingPair& Other)
		{
			return Other.ViewportIdentifier == ViewportIdentifier;
		});

	if (Pair)
	{
		return &Pair->DisplayConfiguration;
	}

	return nullptr;
}

void UOpenColorIOLevelViewportSettings::SetViewportSettings(FName ViewportIdentifier, const FOpenColorIODisplayConfiguration& Configuration)
{
	FPerViewportDisplaySettingPair* Pair = ViewportsSettings.FindByPredicate([ViewportIdentifier](const FPerViewportDisplaySettingPair& Other)
		{
			return Other.ViewportIdentifier == ViewportIdentifier;
		});

	if (Pair)
	{
		Pair->DisplayConfiguration = Configuration;
	}
	else
	{
		//Add new entry if viewport is not found
		FPerViewportDisplaySettingPair NewEntry;
		NewEntry.ViewportIdentifier = ViewportIdentifier;
		NewEntry.DisplayConfiguration = Configuration;
		ViewportsSettings.Emplace(MoveTemp(NewEntry));
	}
}
