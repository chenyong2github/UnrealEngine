// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineConfigBase.h"

#include "MoviePipelineMasterConfig.generated.h"


// Forward Declares
class ULevelSequence;
class UMoviePipelineSetting;
class UMoviePipelineRenderPass;
class UMoviePipelineOutputBase;
class UMoviePipelineShotConfig;

/**
* This class describes the main configuration for a Movie Render Pipeline.
* Only settings that apply to the entire output should be stored here,
* anything that is changed on a per-shot basis should be stored inside of 
* UMovieRenderShotConfig instead.
*
* THIS CLASS SHOULD BE IMMUTABLE ONCE PASSED TO THE PIPELINE FOR PROCESSING.
* (Otherwise you will be modifying the instance that exists in the UI)
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineMasterConfig : public UMoviePipelineConfigBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelineMasterConfig();

public:
	TArray<UMoviePipelineOutputBase*> GetOutputContainers() const;

public:

	/** Returns a pointer to the config specified for the shot, otherwise the default for this pipeline. */
	UMoviePipelineShotConfig* GetConfigForShot(const FString& ShotName) const;

protected:
	virtual bool CanSettingBeAdded(UMoviePipelineSetting* InSetting) override
	{
		check(InSetting);
		return InSetting->IsValidOnMaster();
	}
public:
	
	/** The default shot-setup to use for any shot that doesn't a specific implementation. This is required! */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Movie Render Pipeline")
	UMoviePipelineShotConfig* DefaultShotConfig;
	
	/** A mapping of Shot Name -> Shot Config to use for rendering specific shots with specific configs. */
	UPROPERTY(VisibleAnywhere, Instanced, BlueprintReadOnly, Category = "Movie Render Pipeline")
	TMap<FString, UMoviePipelineShotConfig*> PerShotConfigMapping;
};