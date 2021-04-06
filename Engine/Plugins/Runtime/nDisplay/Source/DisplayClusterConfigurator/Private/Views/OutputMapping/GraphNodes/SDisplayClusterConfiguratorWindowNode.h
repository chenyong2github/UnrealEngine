// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorBaseNode.h"

#include "UObject/WeakObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FDisplayClusterConfiguratorOutputMappingWindowSlot;
class FDisplayClusterConfiguratorBlueprintEditor;
class IDisplayClusterConfiguratorTreeItem;
class UDisplayClusterConfigurationClusterNode;
class UDisplayClusterConfiguratorWindowNode;
class SDisplayClusterConfiguratorExternalImage;

class SDisplayClusterConfiguratorWindowNode
	: public SDisplayClusterConfiguratorBaseNode
{
public:
	friend class SNodeInfo;

	~SDisplayClusterConfiguratorWindowNode();

	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorWindowNode)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs,
		UDisplayClusterConfiguratorWindowNode* InWindowNode,
		const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter) override;
	virtual bool CanBeSelected(const FVector2D& MousePositionInNode) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FVector2D GetPosition() const override;
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const override;
	//~ End SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual int32 GetNodeLayerIndex() const override;
	virtual bool CanNodeOverlapSiblings() const override { return false; }
	virtual bool CanNodeExceedParentBounds() const override;
	virtual bool CanNodeBeSnapAligned() const override { return true; }
	//~ End SDisplayClusterConfiguratorBaseNode interface

private:
	TSharedRef<SWidget> CreateCornerImageWidget();
	TSharedRef<SWidget> CreateInfoWidget();
	TSharedRef<SWidget> CreateBackground(const TAttribute<FSlateColor>& ColorAndOpacity);

	const FSlateBrush* GetBorderBrush() const;
	int32 GetBorderLayerOffset() const;
	const FSlateBrush* GetNodeShadowBrush() const;
	FMargin GetBackgroundPosition() const;
	FMargin GetAreaResizeHandlePosition() const;
	EVisibility GetAreaResizeHandleVisibility() const;
	bool IsAspectRatioFixed() const;
	FSlateColor GetCornerColor() const;
	FVector2D GetPreviewImageSize() const;
	EVisibility GetPreviewImageVisibility() const;

	bool CanShowInfoWidget() const;
	bool CanShowCornerImageWidget() const;
	bool IsClusterNodeLocked() const;

	void OnPreviewImageChanged();

private:
	TSharedPtr<SWidget> CornerImageWidget;
	TSharedPtr<SWidget> InfoWidget;
	TSharedPtr<SDisplayClusterConfiguratorExternalImage> PreviewImageWidget;

	FVector2D WindowScaleFactor;

	FDelegateHandle ImageChangedHandle;

public:
	static int32 const DefaultZOrder;
};
