// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorViewTree.h"

class FDisplayClusterConfiguratorToolkit;

class FDisplayClusterConfiguratorViewScene
	: public FDisplayClusterConfiguratorViewTree
{
public:
	FDisplayClusterConfiguratorViewScene(const TSharedRef<FDisplayClusterConfiguratorToolkit>& Toolkit);
};
