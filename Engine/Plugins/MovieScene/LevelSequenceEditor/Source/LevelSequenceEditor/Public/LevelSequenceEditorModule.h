// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"


class UObject;


/**
 * Interface for the Level Sequence Editor module.
 */
class ILevelSequenceEditorModule
	: public IModuleInterface
{
public:
	DECLARE_EVENT_OneParam(ILevelSequenceEditorModule, FOnLevelSequenceWithShotsCreated, UObject*);
	virtual FOnLevelSequenceWithShotsCreated& OnLevelSequenceWithShotsCreated() = 0;

	DECLARE_MULTICAST_DELEGATE_OneParam(FAllowPlaybackContext, bool&);
	virtual FAllowPlaybackContext& OnComputePlaybackContext() = 0;
};
