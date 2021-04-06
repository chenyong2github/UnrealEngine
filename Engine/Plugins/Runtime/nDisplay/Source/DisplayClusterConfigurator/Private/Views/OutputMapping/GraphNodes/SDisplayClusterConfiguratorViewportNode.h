// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDisplayClusterConfiguratorOutputMappingViewportSlot;
class FDisplayClusterConfiguratorBlueprintEditor;
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

	~SDisplayClusterConfiguratorViewportNode();
	
	void Construct(const FArguments& InArgs,
		UDisplayClusterConfiguratorViewportNode* InViewportNode,
		const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter) override;
	//~ End of SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual bool IsNodeVisible() const override;
	virtual int32 GetNodeLayerIndex() const override { return DefaultZOrder; }
	virtual bool CanNodeOverlapSiblings() const override { return false; }
	virtual bool CanNodeBeSnapAligned() const override { return true; }
	//~ End of SDisplayClusterConfiguratorBaseNode interface

	void SetPreviewTexture(UTexture* InTexture);

private:
	FSlateColor GetBackgroundColor() const;
	const FSlateBrush* GetBackgroundBrush() const;
	const FSlateBrush* GetNodeShadowBrush() const;
	const FSlateBrush* GetBorderBrush() const;
	FSlateColor GetTextBoxColor() const;
	FText GetPositionAndSizeText() const;
	FMargin GetBackgroundPosition() const;
	FMargin GetAreaResizeHandlePosition() const;
	EVisibility GetAreaResizeHandleVisibility() const;
	bool IsAspectRatioFixed() const;
	bool IsViewportLocked() const;
	EVisibility GetLockIconVisibility() const;

private:
	FSlateBrush BackgroundActiveBrush;
	TSharedPtr<SImage> BackgroundImage;

public:
	static const int32 DefaultZOrder;
};
