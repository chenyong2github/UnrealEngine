// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "MoviePipelineSetting.h"
#include "Engine/EngineTypes.h"

#include "MoviePipelineShotConfig.generated.h"


// Forward Declares
class ULevelSequence;
class UMoviePipelineSetting;
class UMoviePipelineRenderPass;
class UMoviePipelineOutput;

UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineShotConfig : public UObject
{
	GENERATED_BODY()
	
public:
	UMoviePipelineShotConfig()
	{
		SettingsSerialNumber = -1;
	}

	bool ValidateConfig(TArray<FText>& OutValidationErrors) const;
	void RemoveSetting(UMoviePipelineSetting* InSetting);
	int32 GetSettingsSerialNumber() const { return SettingsSerialNumber; }
	TArray<UMoviePipelineSetting*> GetSettings() const { return Settings; }

public:
	/**
	* Find a setting of a particular type for this config.
	* @param InClass - Class that you wish to find the setting object for.
	* @return An instance of this class if it already exists as a setting on this config, otherwise null.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineSetting* FindSettingByClass(TSubclassOf<UMoviePipelineSetting> InClass) const
	{
		UMoviePipelineSetting* const* Found = Settings.FindByPredicate([InClass](UMoviePipelineSetting* In) { return In && In->GetClass() == InClass; });
		return Found ? CastChecked<UMoviePipelineSetting>(*Found) : nullptr;
	}
	
	/** Finds a setting of a particular type for this pipeline config, adding it if it doesn't already exist.
	* @param InClass - Class you wish to find or create the setting object for.
	* @return An instance of this class as a setting on this config. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	UMoviePipelineSetting* FindOrAddSettingByClass(TSubclassOf<UMoviePipelineSetting> InClass)
	{
		UMoviePipelineSetting* Found = FindSettingByClass(InClass);
		if (!Found)
		{
			Modify();

			Found = NewObject<UMoviePipelineSetting>(this, InClass);
			Settings.Add(Found);
			++SettingsSerialNumber;
		}
	
		return Found;
	}
	
	/**
	* Find a setting of a particular type for this config
	*/ 
	template<typename SettingType>
	SettingType* FindSetting() const
	{
		UClass* PredicateClass = SettingType::StaticClass();
		UMoviePipelineSetting* const* Found = Settings.FindByPredicate([PredicateClass](UMoviePipelineSetting* In) { return In && In->GetClass() == PredicateClass; });
		return Found ? CastChecked<SettingType>(*Found) : nullptr;
	}
	
	/**
	 * Find a setting of a particular type for this config instance, adding one if it was not found.
	 */
	template<typename SettingType>
	SettingType* FindOrAddSetting()
	{
		SettingType* Found = FindSetting<SettingType>();
		if (!Found)
		{
			Modify();

			Found = NewObject<SettingType>(this);
			Settings.Add(Found);
			++SettingsSerialNumber;
		}
		return Found;
	}
public:
	/** What Sequence do we want to render out. Can be overridden by command line launch arguments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Movie Pipeline", meta=(MetaClass="LevelSequence"))
	FSoftObjectPath Sequence;
	
	/** What resolution should our output files be exported at? */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FVector2D OutputResolution;
	
	/** What frame rate should the output files be exported at? This overrides the Display Rate of the target sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FFrameRate OutputFrameRate;
	
	/** What directory should all of our output files be relative to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	FDirectoryPath OutputDirectory;
	
	/** If true, output containers should attempt to override any existing files with the same name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	bool bOverrideExistingOutput;
	
	/** Array of settings classes that affect various parts of the output pipeline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	TArray<UMoviePipelineSetting*> Settings;
	
	/** Array of Input Buffers. These specify how many render passes should happen each frame to get the data for that frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	TArray<UMoviePipelineRenderPass*> InputBuffers;
	
	/** Array of Output Containers. Each output container is passed data for each Input Buffer every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movie Pipeline")
	TArray<UMoviePipelineOutput*> OutputContainers;

private:
	int32 SettingsSerialNumber;
};