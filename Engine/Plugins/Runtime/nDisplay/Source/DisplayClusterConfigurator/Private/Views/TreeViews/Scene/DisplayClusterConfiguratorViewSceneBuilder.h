// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorTreeBuilder.h"

class FDisplayClusterConfiguratorToolkit;
class UDisplayClusterConfigurationData;
class UDisplayClusterConfigurationScene;

class FDisplayClusterConfiguratorViewSceneBuilder
	: public FDisplayClusterConfiguratorTreeBuilder
{
public:
	FDisplayClusterConfiguratorViewSceneBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin IDisplayClusterConfiguratorTreeBuilder Interface
	virtual void Build(FDisplayClusterConfiguratorTreeBuilderOutput& Output) override;
	//~ End IDisplayClusterConfiguratorTreeBuilder Interface

private:
	void AddScene(FDisplayClusterConfiguratorTreeBuilderOutput& Output, UDisplayClusterConfigurationData* InConfigurationTempConfig, UDisplayClusterConfigurationScene* InObjectToEdit);
};
