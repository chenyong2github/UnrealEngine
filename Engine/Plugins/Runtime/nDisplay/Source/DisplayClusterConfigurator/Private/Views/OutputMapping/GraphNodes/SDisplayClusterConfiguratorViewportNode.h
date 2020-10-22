// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDisplayClusterConfiguratorOutputMappingViewportSlot;
class FDisplayClusterConfiguratorToolkit;
class IDisplayClusterConfiguratorTreeItem;
class SImage;
class UDisplayClusterConfigurationViewport;
class UDisplayClusterConfiguratorViewportNode;

class SDisplayClusterConfiguratorViewportNode
	: public SDisplayClusterConfiguratorBaseNode
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewportNode)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs,
		UDisplayClusterConfiguratorViewportNode* InViewportNode,
		const TSharedRef<FDisplayClusterConfiguratorOutputMappingViewportSlot>& InViewportSlot,
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	//~ End of SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual UObject* GetEditingObject() const override;
	virtual void SetNodePositionOffset(const FVector2D InLocalOffset) override;
	virtual void SetNodeSize(const FVector2D InLocalSize) override;
	virtual void OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;

	virtual void SetBackgroundDefaultBrush() override;

	virtual void SetBackgroundBrushFromTexture(UTexture* InTexture) override;
	//~ End of SDisplayClusterConfiguratorBaseNode interface

private:
	FSlateColor GetDefaultBackgroundColor() const;
	FSlateColor GetImageBackgroundColor() const;

	const FSlateBrush* GetBorderBrush() const;
	FText GetPositionAndSizeText() const;

	FMargin GetAreaResizeHandlePosition() const;

private:
	TWeakObjectPtr<UDisplayClusterConfiguratorViewportNode> ViewportNodePtr;

	TWeakPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlotPtr;

	TWeakObjectPtr<UDisplayClusterConfigurationViewport> CfgViewportPtr;

	FSlateBrush BackgroundActiveBrush;

	TSharedPtr<SImage> BackgroundImage;
};
