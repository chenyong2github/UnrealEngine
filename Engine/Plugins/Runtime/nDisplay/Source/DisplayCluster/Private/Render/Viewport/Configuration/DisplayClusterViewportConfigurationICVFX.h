// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_RenderSettingsICVFX.h"

class FDisplayClusterViewportManager;
class FDisplayClusterViewport;
class ADisplayClusterRootActor;

class FDisplayClusterViewportConfigurationICVFX
{
public:
	FDisplayClusterViewportConfigurationICVFX(ADisplayClusterRootActor& InRootActor);
	~FDisplayClusterViewportConfigurationICVFX();

public:
	void Update();
	void PostUpdate();

private:
	void ImplBeginReallocateViewports(FDisplayClusterViewportManager& ViewportManager);
	void ImplFinishReallocateViewports(FDisplayClusterViewportManager& ViewportManager);

	void ImplGetCameras();
	EDisplayClusterViewportICVFXFlags ImplGetTargetViewports(FDisplayClusterViewportManager& ViewportManager, TArray<FDisplayClusterViewport*>& OutTargets);

	bool CreateLightcardViewport(FDisplayClusterViewport& BaseViewport);

	void UpdateHideList(FDisplayClusterViewportManager& ViewportManager);
	void UpdateCameraHideList(FDisplayClusterViewportManager& ViewportManager);

private:
	ADisplayClusterRootActor& RootActor;
	TArray<class FDisplayClusterViewportConfigurationCameraICVFX>& StageCameras;
};

