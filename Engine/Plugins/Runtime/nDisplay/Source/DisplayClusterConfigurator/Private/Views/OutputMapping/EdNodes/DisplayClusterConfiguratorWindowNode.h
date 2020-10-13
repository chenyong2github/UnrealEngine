// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorWindowNode.generated.h"

class UDisplayClusterConfigurationClusterNode;

UCLASS()
class UDisplayClusterConfiguratorWindowNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	UDisplayClusterConfigurationClusterNode* GetCfgClusterNode();

public:
	FLinearColor CornerColor;
};

