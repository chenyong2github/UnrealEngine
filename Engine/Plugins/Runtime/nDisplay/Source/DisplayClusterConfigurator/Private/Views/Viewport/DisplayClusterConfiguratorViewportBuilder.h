// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/Viewport/IDisplayClusterConfiguratorViewportBuilder.h"

class AActor;
class FDisplayClusterConfiguratorToolkit;
class FDisplayClusterConfiguratorPreviewScene;
class UActorComponent;
class ADisplayClusterRootActor;


class FDisplayClusterConfiguratorViewportBuilder
	: public IDisplayClusterConfiguratorViewportBuilder
{
public:
	FDisplayClusterConfiguratorViewportBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene);

public:
	//~ Begin IDisplayClusterConfiguratorViewportBuilder Interface
	virtual void BuildViewport() override;
	virtual void ClearViewportSelection() override;
	//~ End IDisplayClusterConfiguratorViewportBuilder Interface

public:
	void UpdateOutputMappingPreview();

protected:
	void ResetScene(UWorld* World);

private:
	void OnClearViewportSelection();
	void OnObjectSelected();

private:
	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScenePtr;

	ADisplayClusterRootActor* RootActor = nullptr;
	TSharedPtr<TMap<UObject*, FString>> ObjectsNameMap;
};
