// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturesSubsystemSettings.h"
#include "Misc/Paths.h"

const FName UGameFeaturesSubsystemSettings::LoadStateClient(TEXT("Client"));
const FName UGameFeaturesSubsystemSettings::LoadStateServer(TEXT("Server"));

UGameFeaturesSubsystemSettings::UGameFeaturesSubsystemSettings()
	: BuiltInGameFeaturePluginsFolder(FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() + TEXT("GameFeatures/")))
{
}
