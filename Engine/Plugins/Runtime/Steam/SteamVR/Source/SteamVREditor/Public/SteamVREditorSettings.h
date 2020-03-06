// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SteamVREditorSettings.generated.h"

/**
 * 
 */
UCLASS(config = Editor, defaultconfig)
class STEAMVREDITOR_API USteamVREditorSettings : public UObject
{
	GENERATED_BODY()
public:
	/** Whether or not to show the SteamVR Input settings toolbar button */
	UPROPERTY(config, EditAnywhere, Category = "SteamVR Editor Settings")
	bool bShowSteamVrInputToolbarButton;
};
