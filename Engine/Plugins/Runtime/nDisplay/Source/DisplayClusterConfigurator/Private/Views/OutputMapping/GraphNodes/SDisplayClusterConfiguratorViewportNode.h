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
class UTexture;

class SDisplayClusterConfiguratorViewportNode
	: public SDisplayClusterConfiguratorBaseNode
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewportNode)
	{}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs,
		UDisplayClusterConfiguratorViewportNode* InViewportNode,
		const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End of SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual bool IsNodeVisible() const override;
	virtual bool CanNodeBeSnapAligned() const override { return true; }
	virtual bool CanNodeBeResized() const { return !IsViewportLocked(); }
	virtual float GetNodeMinimumSize() const override;
	virtual float GetNodeMaximumSize() const override;
	virtual bool IsAspectRatioFixed() const override;
	//~ End of SDisplayClusterConfiguratorBaseNode interface

private:
	FSlateColor GetBackgroundColor() const;
	const FSlateBrush* GetBackgroundBrush() const;
	const FSlateBrush* GetNodeShadowBrush() const;
	const FSlateBrush* GetBorderBrush() const;
	FSlateColor GetTextBoxColor() const;
	FText GetPositionAndSizeText() const;
	FMargin GetBackgroundPosition() const;
	FMargin GetAreaResizeHandlePosition() const;
	bool IsViewportLocked() const;
	EVisibility GetLockIconVisibility() const;

	void UpdatePreviewTexture();

private:
	FSlateBrush BackgroundActiveBrush;
	TSharedPtr<SImage> BackgroundImage;

	UTexture* CachedTexture;
};
