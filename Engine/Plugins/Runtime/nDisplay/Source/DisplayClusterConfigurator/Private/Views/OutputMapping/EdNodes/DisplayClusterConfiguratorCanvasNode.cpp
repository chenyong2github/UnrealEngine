// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"

#include "DisplayClusterConfigurationTypes.h"

UDisplayClusterConfigurationCluster* UDisplayClusterConfiguratorCanvasNode::GetCfgCluster()
{
	return Cast<UDisplayClusterConfigurationCluster>(ObjectToEdit.Get());
}
