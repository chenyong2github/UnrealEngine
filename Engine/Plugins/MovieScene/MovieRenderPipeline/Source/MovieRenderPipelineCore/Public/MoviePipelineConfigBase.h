// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineConfigBase.generated.h"


UCLASS(BlueprintType, Abstract)
class MOVIERENDERPIPELINECORE_API UMoviePipelineConfigBase : public UObject
{
	GENERATED_BODY()

public:
	UMoviePipelineConfigBase()
	{
		SettingsSerialNumber = -1;
	}

public:
	/**
	* Removes the specific instance from our Setting list.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void RemoveSetting(UMoviePipelineSetting* InSetting);

	virtual void CopyFrom(UMoviePipelineConfigBase* InConfig);

	int32 GetSettingsSerialNumber() const { return SettingsSerialNumber; }

	/**
	* Returns an array of all settings in this config.
	*/
	virtual TArray<UMoviePipelineSetting*> GetSettings() const { return Settings; }

public:

	/**
	* Find a setting of a particular type for this config.
	* @param InClass - Class that you wish to find the setting object for.
	* @return An instance of this class if it already exists as a setting on this config, otherwise null.
	*/
	UFUNCTION(BlueprintPure, meta = (DeterminesOutputType = "InClass"), Category = "Movie Render Pipeline")
	UMoviePipelineSetting* FindSettingByClass(TSubclassOf<UMoviePipelineSetting> InClass) const
	{
		TArray<UMoviePipelineSetting*> AllSettings = GetSettings();
		UMoviePipelineSetting* const* Found = AllSettings.FindByPredicate([InClass](UMoviePipelineSetting* In) { return In && In->GetClass() == InClass; });
		return Found ? CastChecked<UMoviePipelineSetting>(*Found) : nullptr;
	}

	/**
	* Finds a setting of a particular type for this pipeline config, adding it if it doesn't already exist.
	* @param InClass - Class you wish to find or create the setting object for.
	* @return An instance of this class as a setting on this config.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeterminesOutputType = "InClass"), Category = "Movie Render Pipeline")
	UMoviePipelineSetting* FindOrAddSettingByClass(TSubclassOf<UMoviePipelineSetting> InClass)
	{
		UMoviePipelineSetting* Found = FindSettingByClass(InClass);
		if (!Found)
		{
			Modify();
			
			Found = NewObject<UMoviePipelineSetting>(this, InClass);

			if (CanSettingBeAdded(Found))
			{
				Settings.Add(Found);
				++SettingsSerialNumber;
			}
			else
			{
				FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Setting %d is not compatible with this Config Type and was not added."), *InClass->GetName()), ELogVerbosity::Error);
				return nullptr;
			}
		}

		return Found;
	}

	/**
	* Finds an array of all settings matching the specific type, including inherited classes.
	*/
	template<typename SettingType>
	TArray<SettingType*> FindSettings() const
	{
		TArray<SettingType*> FoundSettings;

		TArray<UMoviePipelineSetting*> AllSettings = GetSettings();
		for (UMoviePipelineSetting* Setting : AllSettings)
		{
			if (Setting->GetClass()->IsChildOf<SettingType>())
			{
				FoundSettings.Add(Cast<SettingType>(Setting));
			}
		}

		return FoundSettings;
	}

	/**
	* Find a setting of a particular type for this config
	*/
	template<typename SettingType>
	SettingType* FindSetting() const
	{
		UClass* PredicateClass = SettingType::StaticClass();

		TArray<UMoviePipelineSetting*> AllSettings = GetSettings();
		UMoviePipelineSetting* const* Found = AllSettings.FindByPredicate([PredicateClass](UMoviePipelineSetting* In) { return In && In->GetClass() == PredicateClass; });
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
			if (CanSettingBeAdded(Found))
			{
				Settings.Add(Found);
				++SettingsSerialNumber;
			}
			else
			{
				FFrame::KismetExecutionMessage(*FString::Printf(TEXT("Setting %d is not compatible with this Config Type and was not added."), *Found->GetName()), ELogVerbosity::Error);
				return nullptr;
			}
		}
		return Found;
	}

public:
	virtual bool CanSettingBeAdded(const UMoviePipelineSetting* InSetting) const PURE_VIRTUAL( UMoviePipelineConfigBase::CanSettingBeAdded, return false; );

protected:
	/** Array of settings classes that affect various parts of the output pipeline. */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Movie Pipeline")
	TArray<UMoviePipelineSetting*> Settings;

private:
	int32 SettingsSerialNumber;
};