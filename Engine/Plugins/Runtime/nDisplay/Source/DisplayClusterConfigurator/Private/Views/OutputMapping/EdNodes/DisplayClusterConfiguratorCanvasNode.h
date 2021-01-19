// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorBaseNode.h"

#include "DisplayClusterConfiguratorCanvasNode.generated.h"

class UDisplayClusterConfigurationCluster;
class UDisplayClusterConfiguratorWindowNode;

UCLASS()
class UDisplayClusterConfiguratorCanvasNode final
	: public UDisplayClusterConfiguratorBaseNode
{
	GENERATED_BODY()

public:
	//~ Begin EdGraphNode Interface
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;
	//~ End EdGraphNode Interface

	void AddWindowNode(UDisplayClusterConfiguratorWindowNode* WindowNode);
	const TArray<UDisplayClusterConfiguratorWindowNode*>& GetChildWindows() const;

private:
	TArray<UDisplayClusterConfiguratorWindowNode*> ChildWindows;
};
