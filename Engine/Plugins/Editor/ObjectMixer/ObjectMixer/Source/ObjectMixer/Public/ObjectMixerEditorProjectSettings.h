// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectMixerEditorProjectSettings.generated.h"

UCLASS(config = ObjectMixer, defaultconfig)
class OBJECTMIXEREDITOR_API UObjectMixerEditorProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	UObjectMixerEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
	{}
	
	/**
	 * If enabled, clicking an item in the mixer list will also select the item in the Scene Outliner.
	 * Alt + Click to select items in mixer without selecting the item in the Scene outliner.
	 * If disabled, selections will not sync unless Alt is held. Effectively, this is the opposite behavior.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Object Mixer")
	bool bSyncSelection = true;

	/**
	 * If false, a new object will be created every time the filter object is accessed.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Object Mixer")
	bool bExpandTreeViewItemsByDefault = true;
};