// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDisplayClusterConfiguratorOutputMappingWindowSlot;
class FDisplayClusterConfiguratorToolkit;
class IDisplayClusterConfiguratorTreeItem;
class UDisplayClusterConfigurationClusterNode;
class UDisplayClusterConfiguratorWindowNode;

class SDisplayClusterConfiguratorWindowNode
	: public SDisplayClusterConfiguratorBaseNode
{
public:
	friend class SNodeInfo;

	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorWindowNode)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs,
		UDisplayClusterConfiguratorWindowNode* InWindowNode,
		const TSharedRef<FDisplayClusterConfiguratorOutputMappingWindowSlot>& InWindowSlot,
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	//~ End SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual UObject* GetEditingObject() const override;
	virtual void SetNodePositionOffset(const FVector2D InLocalOffset) override;
	virtual void SetNodeSize(const FVector2D InLocalSize) override;
	virtual void OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;
	//~ End SDisplayClusterConfiguratorBaseNode interface

	TSharedRef<SWidget> GetCornerImageWidget();
	TSharedRef<SWidget> CreateInfoWidget();

private:
	const FSlateBrush* GetBorderBrush() const;
	FMargin GetAreaResizeHandlePosition() const;

private:
	TWeakObjectPtr<UDisplayClusterConfiguratorWindowNode> WindowNodePtr;

	TWeakPtr<FDisplayClusterConfiguratorOutputMappingWindowSlot> WindowSlotPtr;

	TWeakObjectPtr<UDisplayClusterConfigurationClusterNode> CfgClusterNodePtr;
};
