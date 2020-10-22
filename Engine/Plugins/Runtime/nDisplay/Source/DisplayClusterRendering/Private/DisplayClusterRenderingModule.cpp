// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRenderingModule.h"
#include "DisplayClusterRenderingManager.h"


void FDisplayClusterRenderingModule::RenderSceneToTexture(const FDisplayClusterRenderingParameters& RenderParams)
{
	FDisplayClusterRenderingManager::Get().RenderSceneToTexture(RenderParams);
}

IMPLEMENT_MODULE(FDisplayClusterRenderingModule, DisplayClusterRendering);
