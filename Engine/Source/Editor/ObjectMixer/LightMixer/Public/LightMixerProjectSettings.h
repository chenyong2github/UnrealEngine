// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LightMixerProjectSettings.generated.h"

UCLASS(config = ObjectMixer, defaultconfig)
class LIGHTMIXER_API ULightMixerProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	ULightMixerProjectSettings(const FObjectInitializer& ObjectInitializer)
	{}

	/**
	 * If true, the Object Mixer menu item will be disabled and removed.
	 * This is useful if you only want to use Light Mixer and to avoid cluttering the menus.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Light Mixer", meta = (ConfigRestartRequired = true))
	bool bHideObjectMixerMenuItem = false;
};
