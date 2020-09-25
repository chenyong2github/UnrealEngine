// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/Viewport/IDisplayClusterConfiguratorViewportBuilder.h"

class AActor;
class FDisplayClusterConfiguratorToolkit;
class FDisplayClusterConfiguratorPreviewScene;
class UActorComponent;

class FDisplayClusterConfiguratorViewportBuilder
	: public IDisplayClusterConfiguratorViewportBuilder
{
public:
	FDisplayClusterConfiguratorViewportBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene);

	//~ Begin IDisplayClusterConfiguratorViewportBuilder Interface
	virtual void BuildViewport() override;
	virtual void ClearViewportSelection() override;
	//~ End IDisplayClusterConfiguratorViewportBuilder Interface

private:
	void OnClearViewportSelection();
	void OnObjectSelected();

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScenePtr;

	TArray<UActorComponent*> Components;

	TArray<AActor*> Actors;
};
