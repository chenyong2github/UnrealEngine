// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"

#include "DisplayClusterConfigurationTypes.h"

UDisplayClusterConfigurationClusterNode* UDisplayClusterConfiguratorWindowNode::GetCfgClusterNode()
{
	return Cast<UDisplayClusterConfigurationClusterNode>(ObjectToEdit.Get());
}
