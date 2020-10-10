// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorViewportNode.generated.h"

class UDisplayClusterConfigurationViewport;

UCLASS(MinimalAPI)
class UDisplayClusterConfiguratorViewportNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationViewport* GetCfgViewport();
};
