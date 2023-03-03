// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HoloLensEditorSettings.h: Declares the UHoloLensEditorSettings class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "HoloLensEditorSettings.generated.h"

/**
 * Implements the editor settings for the HoloLens target platform.
 */
UCLASS(config = EditorPerProjectUserSettings)
class UHoloLensEditorSettings
	: public UObject
{
public:
	GENERATED_UCLASS_BODY()

	/**
	 * When true, we will scan for hololens devices during editor startup.  This does increase startup time.
	 * One can also manually add hololens devices with Platforms->Device Manager->Add An Unlisted Device.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HoloLens")
	bool bEditorAutomaticallyDetectsHoloLensDevices = false;
};
