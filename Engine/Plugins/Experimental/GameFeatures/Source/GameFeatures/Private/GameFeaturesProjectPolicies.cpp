// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturesProjectPolicies.h"
#include "GameFeaturesSubsystem.h"

void UDefaultGameFeaturesProjectPolicies::InitGameFeatureManager()
{
	UE_LOG(LogGameFeatures, Log, TEXT("Scanning for built-in game feature plugins"));

	auto AdditionalFilter = [&](const FString& PluginFilename, const FGameFeaturePluginDetails& PluginDetails) -> bool
	{
		return true;
	};

	UGameFeaturesSubsystem::Get().LoadBuiltInGameFeaturePlugins(AdditionalFilter);
}
