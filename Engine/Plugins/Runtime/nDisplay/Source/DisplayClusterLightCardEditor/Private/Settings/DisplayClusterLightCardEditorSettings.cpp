// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLightCardEditorSettings.h"

UDisplayClusterLightCardEditorProjectSettings::UDisplayClusterLightCardEditorProjectSettings()
{
	LightCardTemplateDefaultPath.Path = TEXT("/Game/VP/LightCards");
	LightCardLabelScale = 1.f;
	bDisplayLightCardLabels = false;
}
