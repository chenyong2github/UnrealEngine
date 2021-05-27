// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "RemoteControlUISettings.generated.h"

UCLASS(config = RemoteControlUI)
class REMOTECONTROLUI_API URemoteControlUISettings : public UDeveloperSettings
{
public:
	GENERATED_BODY()

	//~ Begin UDeveloperSettings interface
	virtual FName GetContainerName() const { return TEXT("Project"); }
	virtual FName GetCategoryName() const { return TEXT("Plugins"); };
	virtual FText GetSectionText() const { return NSLOCTEXT("RemoteControlUISettings", "RemoteControlSettingText", "Remote Control UI"); }
	//~ End UDeveloperSettings interface

public:
	/** Show a warning icon for exposed editor-only fields. */
	UPROPERTY(config, EditAnywhere, Category = "Remote Control Preset", DisplayName = "Show a warning when exposing editor-only entities.")
	bool bDisplayInEditorOnlyWarnings = false;

	/** The split widget control ratio between entity tree and details/protocol binding list. */
	UPROPERTY(config)
	float TreeBindingSplitRatio = 0.7;
};