// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorCanvasNode.h"

#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfiguratorStyle.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorCanvasNode.h"
#include "SGraphPanel.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorCanvasNode"

int32 const SDisplayClusterConfiguratorCanvasNode::DefaultZOrder = 0;

void SDisplayClusterConfiguratorCanvasNode::Construct(const FArguments& InArgs, UDisplayClusterConfiguratorCanvasNode* InNode, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{	
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InNode, InToolkit);

	// Inflate visible bounds by 1.05, ensures canvas borders are always visible and not covered by window nodes
	CanvasScaleFactor = 1.05f;

	UpdateGraphNode();
}

void SDisplayClusterConfiguratorCanvasNode::UpdateGraphNode()
{
	SDisplayClusterConfiguratorBaseNode::UpdateGraphNode();
	
	TAttribute<const FSlateBrush*> SelectedBrush = TAttribute<const FSlateBrush*>::Create(TAttribute<const FSlateBrush*>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorCanvasNode::GetSelectedBrush));

	CanvasSizeTextWidget = SNew(SBorder)
	.BorderImage(FEditorStyle::GetBrush("NoBorder"))
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Center)
		.AutoHeight()
		[
			SNew(SHorizontalBox)
											
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.f, 5.f, 5.f, 2.f)
			.HAlign(EHorizontalAlignment::HAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SDisplayClusterConfiguratorCanvasNode::GetCanvasSizeText)
				.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
				.Justification(ETextJustify::Center)
			]
		]
	];

	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"))
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1)
			[
				SNew(SBox)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SBorder)
					.BorderImage(SelectedBrush)
				]
			]
		]
	];
}

void SDisplayClusterConfiguratorCanvasNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	UDisplayClusterConfiguratorCanvasNode* CanvasEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorCanvasNode>();

	// Resize canvas slot
	FBox2D CanvasBounds;

	// Loop through all windows
	for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator WindowIt(CanvasEdNode->GetChildWindows()); WindowIt; ++WindowIt)
	{
		UDisplayClusterConfiguratorWindowNode* WindowNode = *WindowIt;
		if (!WindowNode->GetNodeSize().IsZero())
		{
			CanvasBounds += WindowNode->GetNodeBounds();
		}
	}

	// Loop through all Viewport if all windows with size 0
	if (!CanvasBounds.bIsValid)
	{
		for (TArray<UDisplayClusterConfiguratorWindowNode*>::TConstIterator WindowIt(CanvasEdNode->GetChildWindows()); WindowIt; ++WindowIt)
		{
			UDisplayClusterConfiguratorWindowNode* WindowNode = *WindowIt;
			for (TArray<UDisplayClusterConfiguratorViewportNode*>::TConstIterator ViewportIt(WindowNode->GetChildViewports()); ViewportIt; ++ViewportIt)
			{
				UDisplayClusterConfiguratorViewportNode* ViewportNode = *ViewportIt;
				CanvasBounds += ViewportNode->GetNodeBounds();
			}
		}
	}

	CanvasEdNode->NodePosX = CanvasBounds.Min.X;
	CanvasEdNode->NodePosY = CanvasBounds.Min.Y;
	CanvasEdNode->ResizeNode(CanvasBounds.GetSize());
}

void SDisplayClusterConfiguratorCanvasNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	// Canvas node is not allowed to be moved in general, so add it to the node filter
	NodeFilter.Add(SharedThis(this));

	SGraphNode::MoveTo(NewPosition, NodeFilter);
}

FVector2D SDisplayClusterConfiguratorCanvasNode::ComputeDesiredSize(float) const
{
	FVector2D NodeSize = GetSize();
	FVector2D ScaledSize = FVector2D::ZeroVector;

	if (NodeSize.X > NodeSize.Y)
	{
		ScaledSize.X = NodeSize.X * CanvasScaleFactor;
		ScaledSize.Y = NodeSize.Y + (ScaledSize.X - NodeSize.X);
	}
	else
	{
		ScaledSize.Y = NodeSize.Y * CanvasScaleFactor;
		ScaledSize.X = NodeSize.X + (ScaledSize.Y - NodeSize.Y);
	}

	return ScaledSize;
}

FVector2D SDisplayClusterConfiguratorCanvasNode::GetPosition() const
{
	const FVector2D NodePosition = SDisplayClusterConfiguratorBaseNode::GetPosition();
	const FVector2D NodeSize = GetSize();
	const FVector2D ActualSize = GetDesiredSize();

	// Offset node position by half of the new inflated size to re-center the canvas after it has been inflated by the scale factor
	return NodePosition - (ActualSize - NodeSize) * 0.5f;
}

TArray<FOverlayWidgetInfo> SDisplayClusterConfiguratorCanvasNode::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets = SGraphNode::GetOverlayWidgets(bSelected, WidgetSize);

	const FVector2D TextSize = CanvasSizeTextWidget->GetDesiredSize();

	FOverlayWidgetInfo Info;
	Info.OverlayOffset = FVector2D((WidgetSize.X - TextSize.X) * 0.5f, WidgetSize.Y);
	Info.Widget = CanvasSizeTextWidget;

	Widgets.Add(Info);

	return Widgets;
}

UObject* SDisplayClusterConfiguratorCanvasNode::GetEditingObject() const
{
	UDisplayClusterConfiguratorCanvasNode* CanvasEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorCanvasNode>();
	return CanvasEdNode->GetObject();
}

void SDisplayClusterConfiguratorCanvasNode::OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	UObject* SelectedObject = InTreeItem->GetObject();

	// Select this node
	if (UObject* NodeObject = GetEditingObject())
	{
		if (NodeObject == SelectedObject)
		{
			bIsObjectFocused = true;
			return;
		}
	}

	bIsObjectFocused = false;
}

const FSlateBrush* SDisplayClusterConfiguratorCanvasNode::GetSelectedBrush() const
{
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GetNodeObj()))
	{
		// Selected Case
		return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Selected.Canvas.Brush");
	}

	// Regular case
	return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Regular.Canvas.Brush");
}

FMargin SDisplayClusterConfiguratorCanvasNode::GetBackgroundPosition() const
{
	const FVector2D Size = ComputeDesiredSize(0);
	return FMargin(0, 0, Size.X, Size.Y);
}

FText SDisplayClusterConfiguratorCanvasNode::GetCanvasSizeText() const
{
	const FVector2D NodeSize = GetSize();
	return FText::Format(LOCTEXT("CanvasResolution", "Canvas Resolution {0} x {1}"), FText::AsNumber(FMath::RoundToInt(NodeSize.X)), FText::AsNumber(FMath::RoundToInt(NodeSize.Y)));
}

#undef LOCTEXT_NAMESPACE