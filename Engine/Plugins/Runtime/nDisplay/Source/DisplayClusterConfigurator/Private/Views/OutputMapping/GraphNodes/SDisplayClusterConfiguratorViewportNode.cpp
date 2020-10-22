// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingViewportSlot.h"
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

void SDisplayClusterConfiguratorViewportNode::Construct(const FArguments& InArgs,
	UDisplayClusterConfiguratorViewportNode* InViewportNode,
	const TSharedRef<FDisplayClusterConfiguratorOutputMappingViewportSlot>& InViewportSlot,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InViewportNode, InToolkit);

	ViewportNodePtr = InViewportNode;
	ViewportSlotPtr = InViewportSlot;
	CfgViewportPtr = ViewportNodePtr.Get()->GetCfgViewport();

	UpdateGraphNode();
}

void SDisplayClusterConfiguratorViewportNode::UpdateGraphNode()
{
	SDisplayClusterConfiguratorBaseNode::UpdateGraphNode();

	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	BackgroundImage = SNew(SImage);
	SetBackgroundDefaultBrush();

	FVector2D ViewportSize(CfgViewport->Region.W, CfgViewport->Region.H);

	NodeSlot = &GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Center)
	[
		SNew(SConstraintCanvas)

		+ SConstraintCanvas::Slot()
		.Offset(FMargin(0, 0, 0, 0))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		[
			SAssignNew(NodeSlotBox, SBox)
			.WidthOverride(ViewportSize.X)
			.HeightOverride(ViewportSize.Y)
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
									.Text(FText::FromString(ViewportNodePtr.Get()->GetNodeName()))
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
		]
	];

	NodeSlot->GetWidget()->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorViewportNode::GetNodeVisibility)));
	NodeSlot->SlotSize(ViewportSize);
}

void SDisplayClusterConfiguratorViewportNode::SetBackgroundDefaultBrush()
{
	BackgroundActiveBrush = *FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Body");

	TAttribute<FSlateColor> BackgroundColor = TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorViewportNode::GetDefaultBackgroundColor));

	if (BackgroundImage.IsValid())
	{
		BackgroundImage->SetImage(&BackgroundActiveBrush);
		BackgroundImage->SetColorAndOpacity(BackgroundColor);
	}
}

void SDisplayClusterConfiguratorViewportNode::SetBackgroundBrushFromTexture(UTexture* InTexture)
{	
	if (BackgroundActiveBrush.GetResourceObject() != InTexture && InTexture != nullptr)
	{
		// Reset the Brush
		BackgroundActiveBrush = FSlateBrush();
		BackgroundActiveBrush.SetResourceObject(InTexture);
		BackgroundActiveBrush.ImageSize.X = InTexture->Resource->GetSizeX();
		BackgroundActiveBrush.ImageSize.Y = InTexture->Resource->GetSizeY();

		if (BackgroundImage.IsValid())
		{
			TAttribute<FSlateColor> BackgroundColorImage = TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorViewportNode::GetImageBackgroundColor));

			BackgroundImage->SetImage(&BackgroundActiveBrush);
			BackgroundImage->SetColorAndOpacity(BackgroundColorImage);
		}
	}
}

UObject* SDisplayClusterConfiguratorViewportNode::GetEditingObject() const
{
	return ViewportNodePtr->GetObject();
}

void SDisplayClusterConfiguratorViewportNode::SetNodePositionOffset(const FVector2D InLocalOffset)
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlot = ViewportSlotPtr.Pin();
	check(ViewportSlot.IsValid());
	
	ViewportSlot->SetLocalPosition(ViewportSlot->GetLocalPosition() + InLocalOffset);
}

void SDisplayClusterConfiguratorViewportNode::SetNodeSize(const FVector2D InLocalSize)
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlot = ViewportSlotPtr.Pin();
	check(ViewportSlot.IsValid());

	NodeSlotBox->SetWidthOverride(InLocalSize.X);
	NodeSlotBox->SetHeightOverride(InLocalSize.Y);

	NodeSlot->SlotSize(InLocalSize);

	ViewportSlot->SetLocalSize(InLocalSize);
}

void SDisplayClusterConfiguratorViewportNode::OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	UObject* SelectedObject = InTreeItem->GetObject();

	// Select this node
	if (UObject* NodeObject = GetEditingObject())
	{
		if (NodeObject == SelectedObject)
		{
			InNodeVisibile = true;
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
					InNodeVisibile = true;
					return;
				}
			}
		}
	}

	InNodeVisibile = false;
}

FSlateColor SDisplayClusterConfiguratorViewportNode::GetDefaultBackgroundColor() const
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlot = ViewportSlotPtr.Pin();
	check(ViewportSlot.IsValid());

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);
	
	if (ViewportSlot->IsOutsideParentBoundary())
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

FSlateColor SDisplayClusterConfiguratorViewportNode::GetImageBackgroundColor() const
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlot = ViewportSlotPtr.Pin();
	check(ViewportSlot.IsValid());

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);

	if (ViewportSlot->IsOutsideParentBoundary())
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
		// Regular case
		return FLinearColor::White;
	}
}

const FSlateBrush* SDisplayClusterConfiguratorViewportNode::GetBorderBrush() const
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingViewportSlot> ViewportSlot = ViewportSlotPtr.Pin();
	check(ViewportSlot.IsValid());

	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Viewport.Border.Brush.Selected");
	}
	else
	{
		if (ViewportSlot->IsOutsideParentBoundary())
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
	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	return FText::Format(LOCTEXT("ResAndOffset", "[{0} x {1}] @ {2}, {3}"), CfgViewport->Region.W, CfgViewport->Region.H, CfgViewport->Region.X, CfgViewport->Region.Y);
}

FMargin SDisplayClusterConfiguratorViewportNode::GetAreaResizeHandlePosition() const
{
	UDisplayClusterConfigurationViewport* CfgViewport = CfgViewportPtr.Get();
	check(CfgViewport != nullptr);

	return FMargin(CfgViewport->Region.W, CfgViewport->Region.H, 0.f, 0.f);
}

#undef LOCTEXT_NAMESPACE