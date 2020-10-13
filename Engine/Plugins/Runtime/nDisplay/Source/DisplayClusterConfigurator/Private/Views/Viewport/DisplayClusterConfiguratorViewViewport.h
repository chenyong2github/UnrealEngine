// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/Viewport/IDisplayClusterConfiguratorViewViewport.h"

class IDisplayClusterConfiguratorPreviewScene;
class FDisplayClusterConfiguratorToolkit;
class FDisplayClusterConfiguratorViewportBuilder;
class FDisplayClusterConfiguratorPreviewScene;
class SDisplayClusterConfiguratorViewViewport;


class FDisplayClusterConfiguratorViewViewport
	: public IDisplayClusterConfiguratorViewViewport
{
public:
	FDisplayClusterConfiguratorViewViewport(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

public:
	//~ Begin IDisplayClusterConfiguratorView Interface
	virtual TSharedRef<SWidget> CreateWidget() override;
	virtual void SetEnabled(bool bInEnabled) override;
	//~ End IDisplayClusterConfiguratorView Interface

	//~ Begin IDisplayClusterConfiguratorViewViewport Interface
	virtual TSharedRef<IDisplayClusterConfiguratorPreviewScene> GetPreviewScene() const override;
	//~ End IDisplayClusterConfiguratorViewViewport Interface

private:
	void OnConfigReloaded();
	void OnOutputMappingBuilt();

private:
	TSharedPtr<SDisplayClusterConfiguratorViewViewport> ViewViewport;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TSharedPtr<FDisplayClusterConfiguratorViewportBuilder> PreviewBuilder;

	TSharedPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScene;
};
