// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorCanvasNode.generated.h"

class UDisplayClusterConfigurationCluster;

UCLASS()
class UDisplayClusterConfiguratorCanvasNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationCluster* GetCfgCluster();
};
