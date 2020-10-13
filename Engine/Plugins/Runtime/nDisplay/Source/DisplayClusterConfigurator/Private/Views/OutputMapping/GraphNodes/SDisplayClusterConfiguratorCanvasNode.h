// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class IDisplayClusterConfiguratorOutputMappingSlot;
class FDisplayClusterConfiguratorToolkit;
class FDisplayClusterConfiguratorOutputMappingBuilder;
class UDisplayClusterConfiguratorCanvasNode;
class UDisplayClusterConfigurationCluster;

class SDisplayClusterConfiguratorCanvasNode
	: public SDisplayClusterConfiguratorBaseNode
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorCanvasNode)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UDisplayClusterConfiguratorCanvasNode* InNode, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void SetOwner(const TSharedRef<SGraphPanel>& OwnerPanel) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual UObject* GetEditingObject() const override;
	virtual void OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;
	//~ End of SDisplayClusterConfiguratorBaseNode interface

public:
	const TArray<TSharedPtr<IDisplayClusterConfiguratorOutputMappingSlot>>& GetAllSlots() const;

private:
	TWeakObjectPtr<UDisplayClusterConfiguratorCanvasNode> CanvasNodePtr;

	TWeakObjectPtr<UDisplayClusterConfigurationCluster> CfgClusterPtr;

	TSharedPtr<FDisplayClusterConfiguratorOutputMappingBuilder> OutputMappingBuilder;
};
