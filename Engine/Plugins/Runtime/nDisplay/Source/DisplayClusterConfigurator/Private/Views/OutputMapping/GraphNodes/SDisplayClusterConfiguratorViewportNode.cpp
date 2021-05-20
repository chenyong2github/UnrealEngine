// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorResizer.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorLayeringBox.h"

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
                                                        const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit)
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

	UpdatePreviewTexture();

	GetOrAddSlot( ENodeZone::Center )
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SDisplayClusterConfiguratorLayeringBox)
		.LayerOffset(this, &SDisplayClusterConfiguratorViewportNode::GetNodeVisualLayer)
		.ShadowBrush(this, &SDisplayClusterConfiguratorViewportNode::GetNodeShadowBrush)
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
								SNew(SBorder)
								.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
								.BorderBackgroundColor(this, &SDisplayClusterConfiguratorViewportNode::GetTextBoxColor)
								.Padding(8.0f)
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

									+ SVerticalBox::Slot()
									.VAlign(VAlign_Center)
									.HAlign(HAlign_Center)
									.AutoHeight()
									.Padding(5.0f, 2.0f)
									[
										SNew(SBox)
										.WidthOverride(32)
										.HeightOverride(32)
										.Visibility(this, &SDisplayClusterConfiguratorViewportNode::GetLockIconVisibility)
										[
											SNew(SImage)
											.Image(FEditorStyle::GetBrush(TEXT("GenericLock")))
										]
									]
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
	];
}

void SDisplayClusterConfiguratorViewportNode::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SDisplayClusterConfiguratorBaseNode::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	UpdatePreviewTexture();
}

void SDisplayClusterConfiguratorViewportNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	if (IsViewportLocked())
	{
		NodeFilter.Add(SharedThis(this));
	}

	SDisplayClusterConfiguratorBaseNode::MoveTo(NewPosition, NodeFilter, bMarkDirty);
}

bool SDisplayClusterConfiguratorViewportNode::IsNodeVisible() const
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);
	const bool bIsVisible = bIsSelected || OutputMapping->GetOutputMappingSettings().bShowOutsideViewports || !ViewportEdNode->IsOutsideParent();
	return SDisplayClusterConfiguratorBaseNode::IsNodeVisible() && bIsVisible;
}

float SDisplayClusterConfiguratorViewportNode::GetNodeMinimumSize() const
{
	return UDisplayClusterConfigurationViewport::ViewportMinimumSize;
}

float SDisplayClusterConfiguratorViewportNode::GetNodeMaximumSize() const
{
	return UDisplayClusterConfigurationViewport::ViewportMaximumSize;
}

bool SDisplayClusterConfiguratorViewportNode::IsAspectRatioFixed() const
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	return ViewportEdNode->IsFixedAspectRatio();
}

FSlateColor SDisplayClusterConfiguratorViewportNode::GetBackgroundColor() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();

	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);
	const bool bHasImageBackground = BackgroundActiveBrush.GetResourceObject() != nullptr;
	const bool bIsLocked = IsViewportLocked();
	const bool bTintBackground = OutputMapping->GetOutputMappingSettings().bTintSelectedViewports;

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
			if (bIsSelected && bTintBackground)
			{
				return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.BackgroundImage.Selected");
			}
			else if (bIsLocked)
			{
				return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.BackgroundImage.Locked");
			}
			else
			{
				return FLinearColor::White;
			}
		}
		else
		{
			if (bIsSelected && bTintBackground)
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

const FSlateBrush* SDisplayClusterConfiguratorViewportNode::GetNodeShadowBrush() const
{
	return FEditorStyle::GetBrush(TEXT("Graph.Node.Shadow"));
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

FSlateColor SDisplayClusterConfiguratorViewportNode::GetTextBoxColor() const
{
	const bool bIsSelected = GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode);
	const bool bIsLocked = IsViewportLocked();
	
	if (bIsSelected)
	{
		return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Color.Selected");
	}
	else if (bIsLocked)
	{
		return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.Text.Background.Locked");
	}

	return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Viewport.Text.Background");
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

bool SDisplayClusterConfiguratorViewportNode::IsViewportLocked() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	return OutputMapping->GetOutputMappingSettings().bLockViewports;
}

EVisibility SDisplayClusterConfiguratorViewportNode::GetLockIconVisibility() const
{
	return IsViewportLocked() ? EVisibility::Visible : EVisibility::Collapsed;
}

void SDisplayClusterConfiguratorViewportNode::UpdatePreviewTexture()
{
	UDisplayClusterConfiguratorViewportNode* ViewportEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorViewportNode>();
	UTexture* CurrentTexture = ViewportEdNode->GetPreviewTexture();

	if (CachedTexture != CurrentTexture)
	{
		CachedTexture = CurrentTexture;
		if (CachedTexture != nullptr)
		{
			if (BackgroundActiveBrush.GetResourceObject() != CachedTexture)
			{
				BackgroundActiveBrush = FSlateBrush();
				BackgroundActiveBrush.SetResourceObject(CachedTexture);
				BackgroundActiveBrush.ImageSize.X = CachedTexture->Resource->GetSizeX();
				BackgroundActiveBrush.ImageSize.Y = CachedTexture->Resource->GetSizeY();
			}
		}
		else
		{
			// Reset the brush to be empty.
			BackgroundActiveBrush = FSlateBrush();
		}
	}
}

#undef LOCTEXT_NAMESPACE
