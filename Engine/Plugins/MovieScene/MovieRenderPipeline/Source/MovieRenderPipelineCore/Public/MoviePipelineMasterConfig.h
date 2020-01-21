// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineConfigBase.h"
#include "Misc/FrameRate.h"

#include "MoviePipelineMasterConfig.generated.h"


// Forward Declares
class ULevelSequence;
class UMoviePipelineSetting;
class UMoviePipelineRenderPass;
class UMoviePipelineOutputBase;
class UMoviePipelineShotConfig;
class UMoviePipelineOutputSetting;
class UMoviePipelineExecutorJob;

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
	virtual TArray<UMoviePipelineSetting*> GetSettings() const override;
	virtual void CopyFrom(UMoviePipelineConfigBase* InConfig) override;

public:

	/** Returns a pointer to the config specified for the shot, otherwise the default for this pipeline. */
	UMoviePipelineShotConfig* GetConfigForShot(const FString& ShotName) const;

	void GetFilenameFormatArguments(FFormatNamedArguments& OutArguments, const UMoviePipelineExecutorJob* InJob) const;


	/**
	* Returns the frame rate override from the Master Configuration (if any) or the Sequence frame rate if no override is specified.
	* This should be treated as the actual output framerate of the overall Pipeline.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	FFrameRate GetEffectiveFrameRate(const ULevelSequence* InSequence) const;
protected:
	virtual bool CanSettingBeAdded(const UMoviePipelineSetting* InSetting) const override
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

private:
	UPROPERTY(Transient, Instanced)
	UMoviePipelineOutputSetting* OutputSetting;
};