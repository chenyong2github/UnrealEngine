// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Scalability.h"
#include "Engine/EngineTypes.h"
#include "EditorSettings.generated.h"

UCLASS(config=EditorSettings)
class UNREALED_API UEditorSettings : public UObject
{
	GENERATED_UCLASS_BODY()

	// Derived Data Cache Settings
	// =====================================================================

	/**
	 * Adjusts the local global DDC caching location.  This affects every project on your computer that uses the
	 * UE-LocalDataCachePath environment variable to determine if we're overriding the Local DDC Path, this
	 * is the first location ANY project that doesn't override the DDC path will look for a cache texture, shader...etc.
	 */
	UPROPERTY(EditAnywhere, Category = DerivedDataCache, meta = (DisplayName = "Global Local DDC Path", ConfigRestartRequired = true))
	FDirectoryPath GlobalLocalDDCPath;

	/**
	 * Adjusts the network or shared global DDC caching location.  This is one of the areas queried after Local fails.
	 * This affects every project on your computer that uses the UE-SharedDataCachePath environment variable override.
	 */
	UPROPERTY(EditAnywhere, Category = DerivedDataCache, AdvancedDisplay, meta = (DisplayName = "Global Network DDC Path", ConfigRestartRequired = true))
	FDirectoryPath GlobalSharedDDCPath;

	/**
	 * Directory to be used for caching derived data locally (native textures, compiled shaders, etc...). The editor must be restarted for changes to take effect.
	 * This will override the 'Global Local DDC Path'.
	 */
	UPROPERTY(EditAnywhere, config, Category= DerivedDataCache, AdvancedDisplay, meta = (DisplayName = "Local DDC Path", ConfigRestartRequired = true))
	FDirectoryPath LocalDerivedDataCache;

	/**
	 * Path to a network share that can be used for sharing derived data (native textures, compiled shaders, etc...) with a team. Will not disabled if this directory 
	 * cannot be accessed. The editor must be restarted for changes to take effect, this will override the 'Global Network DDC Path'
	 */
	UPROPERTY(EditAnywhere, config, Category= DerivedDataCache, AdvancedDisplay, meta = (DisplayName = "Network DDC Path", ConfigRestartRequired = true))
	FDirectoryPath SharedDerivedDataCache;

	/** Whether to enable the S3 derived data cache backend */
	UPROPERTY(EditAnywhere, config, Category="Derived Data Cache S3", meta = (DisplayName = "Enable AWS S3 Cache", ConfigRestartRequired = true))
	bool bEnableS3DDC = true;

	/**
	 * Adjusts the local global DDC caching location for AWS/S3 downloaded package bundles.
	 * This affects every project on your computer that uses the UE-S3DataCachePath environment variable override.
	 */
	UPROPERTY(EditAnywhere, Category="Derived Data Cache S3", meta = (DisplayName = "Global Local S3DDC Path", ConfigRestartRequired = true, EditCondition = "bEnableS3DDC"))
	FDirectoryPath GlobalS3DDCPath;

	// =====================================================================

	/** When checked, the most recently loaded project will be auto-loaded at editor startup if no other project was specified on the command line */
	UPROPERTY()
	bool bLoadTheMostRecentlyLoadedProjectAtStartup; // Note that this property is NOT config since it is not necessary to save the value to ini. It is determined at startup in UEditorEngine::InitEditor().

	/** Can the editor report usage analytics (types of assets being spawned, etc...) back to Epic in order for us to improve the editor user experience?  Note: The editor must be restarted for changes to take effect. */
	UPROPERTY()
	bool bEditorAnalyticsEnabled_DEPRECATED;

	// =====================================================================
	// The following options are NOT exposed in the preferences Editor
	// (usually because there is a different way to set them interactively!)

	/** Game project files that were recently opened in the editor */
	UPROPERTY(config)
	TArray<FString> RecentlyOpenedProjectFiles;

	/** The paths of projects created with the new project wizard. This is used to populate the "Path" field of the new project dialog. */
	UPROPERTY(config)
	TArray<FString> CreatedProjectPaths;

	UPROPERTY(config)
	bool bCopyStarterContentPreference;

	/** The id's of the surveys completed */
	UPROPERTY(config)
	TArray<FGuid> CompletedSurveys;

	/** The id's of the surveys currently in-progress */
	UPROPERTY(config)
	TArray<FGuid> InProgressSurveys;

	UPROPERTY(config)
	float AutoScalabilityWorkScaleAmount;

	/** Engine scalability benchmark results */
	Scalability::FQualityLevels EngineBenchmarkResult;

	/** Load the engine scalability benchmark results. Performs a benchmark if not yet valid. */
	void LoadScalabilityBenchmark();

	/** Auto detects and applies the scalability benchmark */
	void AutoApplyScalabilityBenchmark();

	/** @return true if the scalability benchmark is valid */
	bool IsScalabilityBenchmarkValid() const;

	//~ Begin UObject Interface
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
};
