// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Containers/Ticker.h"

class FChaosVDPlaybackController;
class FChaosVDScene;
class FChaosVisualDebuggerMainUI;

/** Core Implementation of the visual debugger - Owns the systems that are not UI */
class FChaosVDEngine : public FTSTickerObjectBase, public TSharedFromThis<FChaosVDEngine>
{
public:

	void Initialize();
	
	void DeInitialize();
	
	virtual bool Tick(float DeltaTime) override;
	
	TSharedPtr<FChaosVDScene>& GetCurrentScene() { return CurrentScene; };
	TSharedPtr<FChaosVDPlaybackController>& GetPlaybackController() { return PlaybackController; };
	
private:

	TSharedPtr<FChaosVDScene> CurrentScene;
	TSharedPtr<FChaosVDPlaybackController> PlaybackController;
	
	bool bIsInitialized = false;
};
