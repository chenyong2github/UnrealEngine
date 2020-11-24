// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "WidgetBlueprint.h"

#include "SynthesisEditorSettings.generated.h"


UCLASS(config = EditorSettings, defaultconfig, meta = (DisplayName = "Synthesis"))
class USynthesisEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()
};
