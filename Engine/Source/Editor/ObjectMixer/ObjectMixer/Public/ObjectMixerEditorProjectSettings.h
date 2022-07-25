// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"

#include "ObjectMixerEditorProjectSettings.generated.h"

UCLASS(config = Engine, defaultconfig)
class OBJECTMIXEREDITOR_API UObjectMixerEditorProjectSettings : public UObject
{
	GENERATED_BODY()
public:
	
	UObjectMixerEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
	{}

	/**
	 * If false, a new object will be created every time the filter object is accessed.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Object Mixer")
	bool bShouldCacheFilterObject = true;
};
