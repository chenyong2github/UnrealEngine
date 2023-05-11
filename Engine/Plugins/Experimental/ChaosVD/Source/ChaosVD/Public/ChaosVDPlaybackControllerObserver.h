// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"

struct FChaosVDTrackInfo;
class FChaosVDPlaybackController;


/**
 * Class to be used as base to any object that needs to Process Player Controller changes
 */
class FChaosVDPlaybackControllerObserver
{
public:
	virtual ~FChaosVDPlaybackControllerObserver();

protected:
	virtual void RegisterNewController(TWeakPtr<FChaosVDPlaybackController> NewController);
	virtual void HandlePlaybackControllerDataUpdated(TWeakPtr<FChaosVDPlaybackController> InController) {};
	virtual void HandleControllerTrackFrameUpdated(TWeakPtr<FChaosVDPlaybackController> InController, const FChaosVDTrackInfo* UpdatedTrackInfo, FGuid InstigatorGuid){};
	
	TWeakPtr<FChaosVDPlaybackController> PlaybackController;
};
