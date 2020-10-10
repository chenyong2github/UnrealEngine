// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/TreeViews/DisplayClusterConfiguratorViewTree.h"

class FDisplayClusterConfiguratorToolkit;

class FDisplayClusterConfiguratorViewInput
	: public FDisplayClusterConfiguratorViewTree
{
public:
	FDisplayClusterConfiguratorViewInput(const TSharedRef<FDisplayClusterConfiguratorToolkit>& Toolkit);
};
