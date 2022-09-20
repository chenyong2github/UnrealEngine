// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "IWaveformTransformation.h"

#include "WaveformEditorTransformationsSettings.generated.h"

/**
 * Settings to regulate Waveform Transformations behavior inside Waveform Editor plugin.
 */
UCLASS(config = WaveformEditor, defaultconfig, meta = (DisplayName = "Waveform Editor Transformations"))
class UWaveformEditorTransformationsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const
	{
		return TEXT("Plugins");
	}

#if WITH_EDITOR
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("WaveformEditorTransformations", "WaveformEditorTransformationsSettingsSection", "Waveform Editor Transformations");
	}

	virtual FName GetSectionName() const override
	{
		return TEXT("Waveform Editor Transformations");
	}

#endif
	//~ End UDeveloperSettings interface
	
public:
	/** A Transformation chain that will be added to the inspected Soundwave if there aren't any  */
	UPROPERTY(config, EditAnywhere, Category = "Launch Options")
	TArray<TSubclassOf<UWaveformTransformationBase>> LaunchTransformations;

};
