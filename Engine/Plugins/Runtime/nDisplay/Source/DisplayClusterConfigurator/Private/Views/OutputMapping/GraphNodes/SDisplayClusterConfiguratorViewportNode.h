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
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter) override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	//~ End of SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual UObject* GetEditingObject() const override;
	virtual void SetNodeSize(const FVector2D InLocalSize, bool bFixedAspectRatio) override;
	virtual void OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;
	virtual bool IsNodeVisible() const override;
	virtual int32 GetNodeLayerIndex() const override { return DefaultZOrder; }
	//~ End of SDisplayClusterConfiguratorBaseNode interface

	void SetPreviewTexture(UTexture* InTexture);

private:
	FSlateColor GetBackgroundColor() const;
	const FSlateBrush* GetBackgroundBrush() const;
	const FSlateBrush* GetBorderBrush() const;
	FText GetPositionAndSizeText() const;
	FMargin GetBackgroundPosition() const;
	FMargin GetAreaResizeHandlePosition() const;
	bool IsAspectRatioFixed() const;

private:
	FSlateBrush BackgroundActiveBrush;
	TSharedPtr<SImage> BackgroundImage;

private:
	static const int32 DefaultZOrder;
};
