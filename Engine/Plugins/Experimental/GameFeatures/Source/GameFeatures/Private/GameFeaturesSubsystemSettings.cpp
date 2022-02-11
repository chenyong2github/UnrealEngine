// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturesSubsystemSettings.h"
#include "Misc/Paths.h"

const FName UGameFeaturesSubsystemSettings::LoadStateClient(TEXT("Client"));
const FName UGameFeaturesSubsystemSettings::LoadStateServer(TEXT("Server"));

UGameFeaturesSubsystemSettings::UGameFeaturesSubsystemSettings()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	BuiltInGameFeaturePluginsFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() + TEXT("GameFeatures/"));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool UGameFeaturesSubsystemSettings::IsValidGameFeaturePlugin(const FString& PluginDescriptorFilename) const
{
	// Build the cache of game feature plugin folders the first time this is called
	if (BuiltInGameFeaturePluginsFolders.IsEmpty())
	{
		BuiltInGameFeaturePluginsFolders.Append(
			FPaths::GetExtensionDirs(FPaths::ProjectDir(), FPaths::Combine(TEXT("Plugins"), TEXT("GameFeatures")))
		);

		for (FString& BuiltInFolder : BuiltInGameFeaturePluginsFolders)
		{
			BuiltInFolder = FPaths::ConvertRelativePathToFull(BuiltInFolder);
		}
	}

	// Check to see if the filename is rooted in a game feature plugin folder
	for (const FString& BuiltInFolder : BuiltInGameFeaturePluginsFolders)
	{
		if (PluginDescriptorFilename.StartsWith(BuiltInFolder))
		{
			return true;
		}
	}

	return false;
}
