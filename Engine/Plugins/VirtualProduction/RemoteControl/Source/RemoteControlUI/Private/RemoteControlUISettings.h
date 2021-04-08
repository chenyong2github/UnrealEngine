// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "RemoteControlUISettings.generated.h"

UCLASS(config = RemoteControlUI)
class REMOTECONTROLUI_API URemoteControlUISettings : public UDeveloperSettings
{
public:
	GENERATED_BODY()

public:
	/** Show a warning icon for exposed editor-only fields. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Preset", DisplayName = "Show a warning when exposing editor-only entities.")
	bool bDisplayInEditorOnlyWarnings = false;
};