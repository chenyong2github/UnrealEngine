// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterLightCardEditorSettings.generated.h"


UCLASS(config = Editor, defaultconfig)
class UDisplayClusterLightCardEditorProjectSettings : public UObject
{
	GENERATED_BODY()

public:
	UDisplayClusterLightCardEditorProjectSettings();

	/** The default path to save new light card templates */
	UPROPERTY(config, EditAnywhere, Category = LightCardTemplates)
	FDirectoryPath LightCardTemplateDefaultPath;
};
