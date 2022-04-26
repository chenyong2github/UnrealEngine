// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleManager.h"

class UMediaPlayer;
class UObject;

/** Log category for this module. */
DECLARE_LOG_CATEGORY_EXTERN(LogMediaPlate, Log, All);

class MEDIAPLATE_API FMediaPlateModule : public IModuleInterface
{
public:
	/**
	 * Call this to get the media player from a media plate object.
	 */
	UMediaPlayer* GetMediaPlayer(UObject* Object);

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** ID for our delegate. */
	int32 GetPlayerFromObjectID = INDEX_NONE;
};
