// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "GameFeaturesSubsystemSettings.generated.h"

/** Settings for the Game Features framework */
UCLASS(config=Game, defaultconfig, notplaceable, meta = (DisplayName = "Game Features"))
class GAMEFEATURES_API UGameFeaturesSubsystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGameFeaturesSubsystemSettings();

	/** State/Bundle to always load on clients */
	static const FName LoadStateClient;

	/** State/Bundle to always load on dedicated server */
	static const FName LoadStateServer;

	/** Name of a singleton class to spawn as the AssetManager, configurable per game. If empty, it will spawn the default one (UDefaultGameFeaturesProjectPolicies) */
	UPROPERTY(config, EditAnywhere, Category=DefaultClasses, meta=(MetaClass="GameFeaturesProjectPolicies", DisplayName="Game Feature Manager Class", ConfigRestartRequired=true))
	FSoftClassPath GameFeaturesManagerClassName;

	/** List of plugins that are forcibly disabled (e.g., via a hotfix) */
	UPROPERTY(config, EditAnywhere, Category=GameFeatures)
	TArray<FString> DisabledPlugins;

	/** List of metadata (additional keys) to try parsing from the .uplugin to provide to FGameFeaturePluginDetails */
	UPROPERTY(config, EditAnywhere, Category=GameFeatures)
	TArray<FString> AdditionalPluginMetadataKeys;

	/** The folder in which all discovered plugins are automatically considered game feature plugins. Plugins outside this folder may also be game features, but need to have bGameFeature: true in the plugin file */
	//@TODO: GameFeaturePluginEnginePush: Make this configurable
	//@TODO: GameFeaturePluginEnginePush: This comment doesn't jive with some of the code in the subsystem which is only paying attention to plugins in this folder
	UPROPERTY(transient)
	FString BuiltInGameFeaturePluginsFolder;
};