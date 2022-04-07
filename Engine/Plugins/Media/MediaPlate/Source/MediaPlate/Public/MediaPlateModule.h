// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class UMediaPlayer;

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaPlate, Log, All);

class MEDIAPLATE_API FMediaPlateModule : public IModuleInterface
{
public:
	/**
	 * Call this to get the UClass for AMediaPlate.
	 */
	virtual UClass* GetAMediaPlateClass();

	/**
	 * Call this to get the media player from a media plate object.
	 */
	virtual UMediaPlayer* GetMediaPlayer(UObject* Object);

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
