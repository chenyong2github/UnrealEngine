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
		const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	//~ Begin SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FVector2D GetPosition() const override;
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter) override;
	virtual TArray<FOverlayWidgetInfo> GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const override;
	//~ End SGraphNode interface

	//~ Begin SDisplayClusterConfiguratorBaseNode interface
	virtual UObject* GetEditingObject() const override;
	virtual void SetNodeSize(const FVector2D InLocalSize, bool bFixedAspectRatio) override;
	virtual void OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem) override;
	virtual int32 GetNodeLayerIndex() const override { return DefaultZOrder; }
	//~ End SDisplayClusterConfiguratorBaseNode interface

private:
	TSharedRef<SWidget> CreateCornerImageWidget();
	TSharedRef<SWidget> CreateInfoWidget();
	TSharedRef<SWidget> CreateBackground(const TAttribute<FSlateColor>& ColorAndOpacity);

	const FSlateBrush* GetBorderBrush() const;
	FMargin GetBackgroundPosition() const;
	FMargin GetAreaResizeHandlePosition() const;
	bool IsAspectRatioFixed() const;

	bool CanShowInfoWidget() const;
	bool CanShowCornerImageWidget() const;

private:
	TSharedPtr<SWidget> CornerImageWidget;
	TSharedPtr<SWidget> InfoWidget;

	FVector2D WindowScaleFactor;

private:
	static int32 const DefaultZOrder;
};
