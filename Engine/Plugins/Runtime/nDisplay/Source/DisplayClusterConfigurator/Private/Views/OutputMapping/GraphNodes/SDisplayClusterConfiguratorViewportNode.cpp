// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorResizer.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "SGraphPanel.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewportNode"

int32 const SDisplayClusterConfiguratorViewportNode::DefaultZOrder = 200;

void SDisplayClusterConfiguratorViewportNode::Construct(const FArguments& InArgs,
	UDisplayClusterConfiguratorViewportNode* InViewportNode,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InViewportNode, InToolkit);

	UpdateGraphNode();
}

void SDisplayClusterConfiguratorViewportNode::UpdateGraphNode()
{
	SDisplayClusterConfiguratorBaseNode::UpdateGraphNode();

	BackgroundImage = SNew(SImage)
		.ColorAndOpacity(this, &SDisplayClusterConfiguratorViewportNode::GetBackgroundColor)
		.Image(this, &SDisplayClusterConfiguratorViewportNode::GetBackgroundBrush);

	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();

	SetPreviewTexture(ViewportEdNode->GetPreviewTexture());

	GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SConstraintCanvas)
		.Visibility(this, &SDisplayClusterConfiguratorViewportNode::GetNodeVisibility)

		+ SConstraintCanvas::Slot()
		.Offset(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorViewportNode::GetBackgroundPosition)))
		.Alignment(FVector2D::ZeroVector)
		[
			SNew(SBox)
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					[
						SNew(SBorder)
						.BorderImage(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Window.Border.Brush"))
						.Padding(FMargin(0.f))
						[
							BackgroundImage.ToSharedRef()
						]
					]

					+ SOverlay::Slot()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.Padding(FMargin(15.f, 12.f))
					[
						SNew(SBox)
						[
							SNew(SScaleBox)
							.Stretch(EStretch::ScaleToFit)
							.StretchDirection(EStretchDirection::DownOnly)
							.VAlign(VAlign_Center)
							[
								SNew( SVerticalBox )

								+ SVerticalBox::Slot()
								.VAlign(VAlign_Center)
								.Padding(5.f, 2.f)
								[
									SNew(STextBlock)
									.Text(FText::FromString(ViewportEdNode->GetNodeName()))
									.Justification(ETextJustify::Center)
									.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Bold"))
									.ColorAndOpacity(FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Text.Color.Regular"))
								]

								+ SVerticalBox::Slot()
								.VAlign(VAlign_Center)
								.Padding(5.f, 2.f)
								[
									SNew(STextBlock)
									.Text(this, &SDisplayClusterConfiguratorViewportNode::GetPositionAndSizeText)
									.Justification(ETextJustify::Center)
									.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
									.ColorAndOpacity(FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Text.Color.WhiteGray"))
								]
							]
						]
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					[
						SNew(SBorder)
						.BorderImage(this, &SDisplayClusterConfiguratorViewportNode::GetBorderBrush)
					]
				]
			]
		]

		+ SConstraintCanvas::Slot()
		.Offset(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorViewportNode::GetAreaResizeHandlePosition)))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		[
			SNew(SDisplayClusterConfiguratorResizer, ToolkitPtr.Pin().ToSharedRef(), SharedThis(this))
			.Visibility(this, &SDisplayClusterConfiguratorViewportNode::GetSelectionVisibility)
			.IsFixedAspectRatio(this, &SDisplayClusterConfiguratorViewportNode::IsAspectRatioFixed)
		]
	];
}

void SDisplayClusterConfiguratorViewportNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	FVector2D CurrentPosition = GetPosition();

	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	FVector2D BestOffset = ViewportEdNode->FindNonOverlappingOffsetFromParent(NewPosition - CurrentPosition);

	SGraphNode::MoveTo(CurrentPosition + BestOffset, NodeFilter);

	ViewportEdNode->UpdateObject();
}

FVector2D SDisplayClusterConfiguratorViewportNode::ComputeDesiredSize(float) const
{
	return GetSize();
}

void SDisplayClusterConfiguratorViewportNode::SetPreviewTexture(UTexture* InTexture)
{
	if (InTexture != nullptr)
	{
		if (BackgroundActiveBrush.GetResourceObject() != InTexture)
		{
			BackgroundActiveBrush = FSlateBrush();
			BackgroundActiveBrush.SetResourceObject(InTexture);
			BackgroundActiveBrush.ImageSize.X = InTexture->Resource->GetSizeX();
			BackgroundActiveBrush.ImageSize.Y = InTexture->Resource->GetSizeY();
		}
	}
	else
	{
		// Reset the brush to be empty.
		BackgroundActiveBrush = FSlateBrush();
	}
}

UObject* SDisplayClusterConfiguratorViewportNode::GetEditingObject() const
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	return ViewportEdNode->GetObject();
}

void SDisplayClusterConfiguratorViewportNode::SetNodeSize(const FVector2D InLocalSize, bool bFixedAspectRatio)
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	const FVector2D BestSize = ViewportEdNode->FindNonOverlappingSizeFromParent(InLocalSize, bFixedAspectRatio);

	GraphNode->ResizeNode(BestSize);
}

void SDisplayClusterConfiguratorViewportNode::OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
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
		else
		{
			// Try to find node object within selected tree child items
			TArray<UObject*> ChildrenObjects;
			InTreeItem->GetChildrenObjectsRecursive(ChildrenObjects);

			for (UObject* ChildObj : ChildrenObjects)
			{
				if (ChildObj == NodeObject)
				{
					bIsObjectFocused = true;
					return;
				}
			}
		}
	}

	bIsObjectFocused = false;
}

bool SDisplayClusterConfiguratorViewportNode::IsNodeVisible() const
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);
	const bool bIsVisible = bIsSelected || OutputMapping->IsShowOutsideViewports() || !ViewportEdNode->IsOutsideParent();
	return SDisplayClusterConfiguratorBaseNode::IsNodeVisible() && bIsVisible;
}

FSlateColor SDisplayClusterConfiguratorViewportNode::GetBackgroundColor() const
{
	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);
	const bool bHasImageBackground = BackgroundActiveBrush.GetResourceObject() != nullptr;

	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	if (ViewportEdNode->IsOutsideParentBoundary())
	{
		if (bIsSelected)
		{
			// Selected Case
			return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.OutsideBackgroundColor.Selected");
		}
		else
		{
			// Regular case
			return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.OutsideBackgroundColor.Regular");
		}
	}
	else
	{
		if (bHasImageBackground)
		{
			// Regular case when there is a background image
			return FLinearColor::White;
		}
		else
		{
			if (bIsSelected)
			{
				// Selected Case
				return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.BackgroundColor.Selected");
			}
			else
			{
				// Regular case
				return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.BackgroundColor.Regular");
			}
		}
	}

}

const FSlateBrush* SDisplayClusterConfiguratorViewportNode::GetBackgroundBrush() const
{
	if (BackgroundActiveBrush.GetResourceObject() != nullptr)
	{
		return &BackgroundActiveBrush;
	}
	else
	{
		return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Body");
	}
}

const FSlateBrush* SDisplayClusterConfiguratorViewportNode::GetBorderBrush() const
{
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Viewport.Border.Brush.Selected");
	}
	else
	{
		UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
		if (ViewportEdNode->IsOutsideParentBoundary())
		{
			return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Viewport.Border.OutsideBrush.Regular");
		}
		else
		{
			return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Viewport.Border.Brush.Regular");
		}
	}
}

FText SDisplayClusterConfiguratorViewportNode::GetPositionAndSizeText() const
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	const FDisplayClusterConfigurationRectangle CfgViewportRegion = ViewportEdNode->GetCfgViewportRegion();

	return FText::Format(LOCTEXT("ResAndOffset", "[{0} x {1}] @ {2}, {3}"), CfgViewportRegion.W, CfgViewportRegion.H, CfgViewportRegion.X, CfgViewportRegion.Y);
}

FMargin SDisplayClusterConfiguratorViewportNode::GetBackgroundPosition() const
{
	const FVector2D NodeSize = GetSize();
	return FMargin(0.f, 0.f, NodeSize.X, NodeSize.Y);
}

FMargin SDisplayClusterConfiguratorViewportNode::GetAreaResizeHandlePosition() const
{
	const FVector2D NodeSize = GetSize();
	return FMargin(NodeSize.X, NodeSize.Y, 0.f, 0.f);
}

bool SDisplayClusterConfiguratorViewportNode::IsAspectRatioFixed() const
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	return ViewportEdNode->IsFixedAspectRatio();
}

#undef LOCTEXT_NAMESPACE