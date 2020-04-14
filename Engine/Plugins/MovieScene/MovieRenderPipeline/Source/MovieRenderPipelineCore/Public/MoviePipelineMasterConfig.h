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
	virtual TArray<UMoviePipelineSetting*> GetUserSettings() const override;
	virtual void CopyFrom(UMoviePipelineConfigBase* InConfig) override;

	/** Initializes a single instance of every setting so that even non-user-configured settings have a chance to apply their default values. Does nothing if they're already instanced for this configuration. */
	void InitializeTransientSettings();

	TArray<UMoviePipelineSetting*> GetTransientSettings() const { return TransientSettings; }
	TArray<UMoviePipelineSetting*> GetAllSettings() const
	{
		TArray<UMoviePipelineSetting*> CombinedSettings;
		CombinedSettings.Append(GetUserSettings());
		CombinedSettings.Append(GetTransientSettings());
		return CombinedSettings;
	}
public:

	/** Returns a pointer to the config specified for the shot, otherwise the default for this pipeline. */
	UMoviePipelineShotConfig* GetConfigForShot(const FString& ShotName) const;

	void GetFilenameFormatArguments(FMoviePipelineFormatArgs& InOutFormatArgs) const;


	/**
	* Returns the frame rate override from the Master Configuration (if any) or the Sequence frame rate if no override is specified.
	* This should be treated as the actual output framerate of the overall Pipeline.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	FFrameRate GetEffectiveFrameRate(const ULevelSequence* InSequence) const;

	TRange<FFrameNumber> GetEffectivePlaybackRange(const ULevelSequence* InSequence) const;

protected:
	virtual bool CanSettingBeAdded(const UMoviePipelineSetting* InSetting) const override
	{
		check(InSetting);
		return InSetting->IsValidOnMaster();
	}

	virtual void OnSettingAdded(UMoviePipelineSetting* InSetting) override;
	virtual void OnSettingRemoved(UMoviePipelineSetting* InSetting) override;

	void AddTransientSettingByClass(const UClass* InSettingClass);
public:	
	/** A mapping of Shot Name -> Shot Config to use for rendering specific shots with specific configs. */
	UPROPERTY(Instanced)
	TMap<FString, UMoviePipelineShotConfig*> PerShotConfigMapping;

private:
	UPROPERTY(Instanced)
	UMoviePipelineOutputSetting* OutputSetting;

	/** An array of settings that are available in the engine and have not been edited by the user. */
	UPROPERTY(Transient)
	TArray<UMoviePipelineSetting*> TransientSettings;
};