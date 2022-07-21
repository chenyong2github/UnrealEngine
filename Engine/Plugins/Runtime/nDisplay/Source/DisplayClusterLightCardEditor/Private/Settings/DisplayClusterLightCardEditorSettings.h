// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterLightCardEditorSettings.generated.h"


UCLASS(config = Editor, defaultconfig, meta = (DisplayClusterMultiUserInclude))
class UDisplayClusterLightCardEditorProjectSettings : public UObject
{
	GENERATED_BODY()

public:
	UDisplayClusterLightCardEditorProjectSettings();

	/** The default path to save new light card templates */
	UPROPERTY(config, EditAnywhere, Category = LightCardTemplates)
	FDirectoryPath LightCardTemplateDefaultPath;

	/** Whether light card labels should be displayed. Handled through the light card editor */
	UPROPERTY()
	bool bDisplayLightCardLabels;
	
	/** The scale to use for light card labels */
	UPROPERTY(config, EditAnywhere, Category = LightCardLabels)
	float LightCardLabelScale;
};
