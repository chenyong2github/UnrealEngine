// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVD/Public/ChaosVDEngine.h"

#include "ChaosVDPlaybackController.h"
#include "ChaosVDScene.h"

void FChaosVDEngine::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	// Create an Empty Scene
	//TODO: Handle multiple scenes. We will need it to represent multiple worlds
	CurrentScene = MakeShared<FChaosVDScene>();
	CurrentScene->Initialize();

	PlaybackController = MakeShared<FChaosVDPlaybackController>(CurrentScene);

	bIsInitialized = true;
}

void FChaosVDEngine::DeInitialize()
{
	if (!bIsInitialized)
	{
		return;
	}

	CurrentScene->DeInitialize();
}
bool FChaosVDEngine::Tick(float DeltaTime)
{
	return true;
}
