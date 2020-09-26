// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorViewTree.h"

class FDisplayClusterConfiguratorToolkit;

class FDisplayClusterConfiguratorViewCluster
	: public FDisplayClusterConfiguratorViewTree
{
public:
	FDisplayClusterConfiguratorViewCluster(const TSharedRef<FDisplayClusterConfiguratorToolkit>& Toolkit);
};
