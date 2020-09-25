// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"

#include "DisplayClusterConfigurationTypes.h"

UDisplayClusterConfigurationViewport* UDisplayClusterConfiguratorViewportNode::GetCfgViewport()
{
	return Cast<UDisplayClusterConfigurationViewport>(ObjectToEdit.Get());
}
