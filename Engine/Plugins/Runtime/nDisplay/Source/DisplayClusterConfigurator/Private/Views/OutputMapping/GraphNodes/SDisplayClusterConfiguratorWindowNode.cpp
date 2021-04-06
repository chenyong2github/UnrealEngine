// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorWindowNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorBlueprintEditor.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorResizer.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorLayeringBox.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorExternalImage.h"

#include "SGraphPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorWindowNode"

int32 const SDisplayClusterConfiguratorWindowNode::DefaultZOrder = 200;

class SCornerImage
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCornerImage)
		: _ColorAndOpacity(FLinearColor::White)
		, _Size(FVector2D(60.f))
		, _ZIndexOffset(0)
	{ }
		SLATE_ATTRIBUTE(FSlateColor, ColorAndOpacity)
		SLATE_ARGUMENT(FVector2D, Size)
		SLATE_ARGUMENT(int32, ZIndexOffset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterConfiguratorWindowNode> InParentNode)
	{
		ParentNode = InParentNode;
		ZIndexOffset = InArgs._ZIndexOffset;

		SetCursor(EMouseCursor::CardinalCross);

		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(InArgs._Size.X)
			.HeightOverride(InArgs._Size.Y)
			[
				SNew(SImage)
				.ColorAndOpacity(InArgs._ColorAndOpacity)
				.Image(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Brush.Corner"))
			]
		];
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// A little hack to ensure that the user can select or drag the parent window node by clicking on the corner widget. The SNodePanel that manages
		// mouse interaction for the graph editor sorts the node widgets by their sort depth to determine which node widget to select and drag, and overlay
		// widgets are not hit tested. By default, windows are always lower than viewports in their sort order to ensure viewports are always selectable over
		// windows, but the one exception is when the user clicks on the corner widget. To ensure that the window widget is selected, increase the window's 
		// z-index temporarily as long as the mouse is over the corner widget.
		SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
		ParentNode->ZIndex += ZIndexOffset;
	}

	void OnMouseLeave(const FPointerEvent& MouseEvent)
	{
		SCompoundWidget::OnMouseLeave(MouseEvent);
		ParentNode->ZIndex -= ZIndexOffset;
	}

private:
	TSharedPtr<SDisplayClusterConfiguratorWindowNode> ParentNode;
	int32 ZIndexOffset;
};

class SNodeInfo
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNodeInfo)
		: _ColorAndOpacity(FLinearColor::White)
		, _ZIndexOffset(0)
	{ }
		SLATE_ATTRIBUTE(FSlateColor, ColorAndOpacity)
		SLATE_ARGUMENT(int32, ZIndexOffset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SDisplayClusterConfiguratorWindowNode>& InParentNode)
	{
		ParentNode = InParentNode;
		ZIndexOffset = InArgs._ZIndexOffset;

		SetCursor(EMouseCursor::CardinalCross);

		ChildSlot
		[
			SNew(SBox)
			.WidthOverride(this, &SNodeInfo::GetTitleWidth)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SScaleBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Fill)
				.Stretch(EStretch::ScaleToFill)
				.StretchDirection(EStretchDirection::DownOnly)
				[
					SNew(SBorder)
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(InArgs._ColorAndOpacity)
					.Padding(FMargin(20, 10, 30, 10))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(36)
							.HeightOverride(36)
							[
								SNew(SImage)
								.Image(FDisplayClusterConfiguratorStyle::GetBrush(TEXT("DisplayClusterConfigurator.TreeItems.ClusterNode")))
							]
						]
		
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SSpacer)
							.Size(FVector2D(15, 1))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SNodeInfo::GetNodeName)
							.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Bold"))
						]
		
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SSpacer)
							.Size(FVector2D(25, 1))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(this, &SNodeInfo::GetPositionAndSizeText)
							.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
						]
		
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SSpacer)
							.Size(FVector2D(25, 1))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(36)
							.HeightOverride(36)
							.Visibility(this, &SNodeInfo::GetLockIconVisibility)
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush(TEXT("GenericLock")))
							]
						]
					]
				]
			]
		];
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// A little hack to ensure that the user can select or drag the parent window node by clicking on the corner widget. The SNodePanel that manages
		// mouse interaction for the graph editor sorts the node widgets by their sort depth to determine which node widget to select and drag, and overlay
		// widgets are not hit tested. By default, windows are always lower than viewports in their sort order to ensure viewports are always selectable over
		// windows, but the one exception is when the user clicks on the corner widget. To ensure that the window widget is selected, increase the window's 
		// z-index temporarily as long as the mouse is over the corner widget.
		SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
		ParentNode->ZIndex += ZIndexOffset;
	}

	void OnMouseLeave(const FPointerEvent& MouseEvent)
	{
		SCompoundWidget::OnMouseLeave(MouseEvent);
		ParentNode->ZIndex -= ZIndexOffset;
	}

	FText GetNodeName() const
	{
		UDisplayClusterConfiguratorWindowNode* WindowEdNode = ParentNode->GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
		const FText NodeName = FText::FromString(WindowEdNode->GetNodeName());
		if (WindowEdNode->IsMaster())
		{
			return FText::Format(LOCTEXT("WindowNameWithMaster", "{0} (Master)"), NodeName);
		}
		else
		{
			return NodeName;
		}
	}

	FText GetPositionAndSizeText() const
	{
		UDisplayClusterConfiguratorWindowNode* WindowEdNode = ParentNode->GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
		FDisplayClusterConfigurationRectangle WindowRect = WindowEdNode->GetCfgWindowRect();
		return FText::Format(LOCTEXT("ResAndOffset", "[{0} x {1}] @ {2}, {3}"), WindowRect.W, WindowRect.H, WindowRect.X, WindowRect.Y);
	}

	FOptionalSize GetTitleWidth() const
	{
		UDisplayClusterConfiguratorWindowNode* WindowEdNode = ParentNode->GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
		return WindowEdNode->NodeWidth;
	}

	EVisibility GetLockIconVisibility() const
	{
		if (ParentNode->IsClusterNodeLocked())
		{
			return EVisibility::Visible;
		}

		return EVisibility::Collapsed;
	}

private:
	TSharedPtr<SDisplayClusterConfiguratorWindowNode> ParentNode;
	int32 ZIndexOffset;
};

SDisplayClusterConfiguratorWindowNode::~SDisplayClusterConfiguratorWindowNode()
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	WindowEdNode->UnregisterOnPreviewImageChanged(ImageChangedHandle);
}

void SDisplayClusterConfiguratorWindowNode::Construct(const FArguments& InArgs,
	UDisplayClusterConfiguratorWindowNode* InWindowNode,
	const TSharedRef<FDisplayClusterConfiguratorBlueprintEditor>& InToolkit)
{
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InWindowNode, InToolkit);

	WindowScaleFactor = FVector2D(1, 1);

	InWindowNode->RegisterOnPreviewImageChanged(UDisplayClusterConfiguratorWindowNode::FOnPreviewImageChangedDelegate::CreateSP(this,
		&SDisplayClusterConfiguratorWindowNode::OnPreviewImageChanged));

	UpdateGraphNode();
}

void SDisplayClusterConfiguratorWindowNode::UpdateGraphNode()
{
	SDisplayClusterConfiguratorBaseNode::UpdateGraphNode();

	CornerImageWidget = CreateCornerImageWidget();
	InfoWidget = CreateInfoWidget();

	GetOrAddSlot(ENodeZone::Center)
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SDisplayClusterConfiguratorLayeringBox)
		.LayerOffset(DefaultZOrder)
		.ShadowBrush(this, &SDisplayClusterConfiguratorWindowNode::GetNodeShadowBrush)
		[
			SNew(SConstraintCanvas)

			+ SConstraintCanvas::Slot()
			.Offset(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorWindowNode::GetBackgroundPosition)))
			.Alignment(FVector2D::ZeroVector)
			[
				SNew(SBox)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					[
						CreateBackground(FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Window.Inner.Background"))
					]

					+ SOverlay::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					[
						SNew(SDisplayClusterConfiguratorLayeringBox)
						.LayerOffset(this, &SDisplayClusterConfiguratorWindowNode::GetBorderLayerOffset)
						[
							SNew(SBorder)
							.BorderImage(this, &SDisplayClusterConfiguratorWindowNode::GetBorderBrush)
						]
					]
				]
			]

			+ SConstraintCanvas::Slot()
			.Offset(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorWindowNode::GetAreaResizeHandlePosition)))
			.AutoSize(true)
			.Alignment(FVector2D::ZeroVector)
			[
				SNew(SDisplayClusterConfiguratorResizer, ToolkitPtr.Pin().ToSharedRef(), SharedThis(this))
				.Visibility(this, &SDisplayClusterConfiguratorWindowNode::GetAreaResizeHandleVisibility)
				.IsFixedAspectRatio(this, &SDisplayClusterConfiguratorWindowNode::IsAspectRatioFixed)
			]
		]
	];
}

void SDisplayClusterConfiguratorWindowNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	if (IsClusterNodeLocked())
	{
		NodeFilter.Add(SharedThis(this));
	}

	SDisplayClusterConfiguratorBaseNode::MoveTo(NewPosition, NodeFilter);
}

bool SDisplayClusterConfiguratorWindowNode::CanBeSelected(const FVector2D& MousePositionInNode) const
{
	if (IsClusterNodeLocked())
	{
		return false;
	}

	return SDisplayClusterConfiguratorBaseNode::CanBeSelected(MousePositionInNode);
}

FVector2D SDisplayClusterConfiguratorWindowNode::ComputeDesiredSize(float) const
{
	return GetSize() * WindowScaleFactor;
}

FVector2D SDisplayClusterConfiguratorWindowNode::GetPosition() const
{
	return SDisplayClusterConfiguratorBaseNode::GetPosition() * WindowScaleFactor;
}

TArray<FOverlayWidgetInfo> SDisplayClusterConfiguratorWindowNode::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets = SDisplayClusterConfiguratorBaseNode::GetOverlayWidgets(bSelected, WidgetSize);

	if (CanShowInfoWidget())
	{
		FOverlayWidgetInfo InfoWidgetInfo;
		InfoWidgetInfo.OverlayOffset = FVector2D::ZeroVector;
		InfoWidgetInfo.Widget = InfoWidget;

		Widgets.Add(InfoWidgetInfo);
	}

	if (CanShowCornerImageWidget())
	{
		FOverlayWidgetInfo CornerWidgetInfo;
		CornerWidgetInfo.OverlayOffset = FVector2D::ZeroVector;
		CornerWidgetInfo.Widget = CornerImageWidget;

		Widgets.Add(CornerWidgetInfo);
	}

	return Widgets;
}

int32 SDisplayClusterConfiguratorWindowNode::GetNodeLayerIndex() const
{
	int32 LayerIndex = DefaultZOrder;

	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	bool bAreViewportsLocked = OutputMapping->GetOutputMappingSettings().bLockViewports;

	// If the alt key is down or viewports are locked, increase the window's layer index so that it is above the viewports, allowing
	// users to select and drag it even if a viewport is in the way.
	if (FSlateApplication::Get().GetModifierKeys().IsAltDown() || bAreViewportsLocked)
	{
		LayerIndex += (SDisplayClusterConfiguratorViewportNode::DefaultZOrder - DefaultZOrder) + 2;
	}

	return LayerIndex;
}

bool SDisplayClusterConfiguratorWindowNode::CanNodeExceedParentBounds() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	return !OutputMapping->GetOutputMappingSettings().bKeepClusterNodesInHosts;
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateCornerImageWidget()
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();

	// Offset the layer of the corner image so that it always draws on top of the viewport graph nodes.
	return SNew(SDisplayClusterConfiguratorLayeringBox)
		.LayerOffset(DefaultZOrder + 101)
		.IsEnabled(this, &SDisplayClusterConfiguratorWindowNode::IsNodeEnabled)
		[
			SNew(SCornerImage, SharedThis(this))
			.ColorAndOpacity(this, &SDisplayClusterConfiguratorWindowNode::GetCornerColor)
			.Size(FVector2D(128))
			.ZIndexOffset((SDisplayClusterConfiguratorViewportNode::DefaultZOrder - DefaultZOrder) + 2)
		];
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateInfoWidget()
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();

	// Offset the layer of the info widget so that it always draws on top of the viewport graph nodes.
	return SNew(SDisplayClusterConfiguratorLayeringBox)
		.LayerOffset(DefaultZOrder + 100)
		.IsEnabled(this, &SDisplayClusterConfiguratorWindowNode::IsNodeEnabled)
		[
			SNew(SNodeInfo, SharedThis(this))
			.ColorAndOpacity(this, &SDisplayClusterConfiguratorWindowNode::GetCornerColor)
			.ZIndexOffset((SDisplayClusterConfiguratorViewportNode::DefaultZOrder - DefaultZOrder) + 2)
		];
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateBackground(const TAttribute<FSlateColor>& InColorAndOpacity)
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();

	return SNew(SOverlay)
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SImage)
			.ColorAndOpacity(InColorAndOpacity)
			.Image(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Body"))
		]
	
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::Both)
			.Visibility(this, &SDisplayClusterConfiguratorWindowNode::GetPreviewImageVisibility)
			[
				SAssignNew(PreviewImageWidget, SDisplayClusterConfiguratorExternalImage)
				.ImagePath(WindowEdNode->GetPreviewImagePath())
				.ShowShadow(false)
				.MinImageSize(FVector2D::ZeroVector)
				.MaxImageSize(this, &SDisplayClusterConfiguratorWindowNode::GetPreviewImageSize)
			]
		];
}

const FSlateBrush* SDisplayClusterConfiguratorWindowNode::GetBorderBrush() const
{
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		// Selected Case
		return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Window.Border.Brush.Selected");
	}

	// Regular case
	return FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Window.Border.Brush.Regular");
}

int32 SDisplayClusterConfiguratorWindowNode::GetBorderLayerOffset() const
{
	// If the window node is selected, we want to render the border at the same layer as the viewport nodes to ensure it is visible
	// in the case that the child viewport nodes completely fill the window, since the border is a key indicator that the window node is selected.
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		return SDisplayClusterConfiguratorViewportNode::DefaultZOrder - DefaultZOrder;
	}

	return 0;
}

const FSlateBrush* SDisplayClusterConfiguratorWindowNode::GetNodeShadowBrush() const
{
	return FEditorStyle::GetBrush(TEXT("Graph.Node.Shadow"));
}

FMargin SDisplayClusterConfiguratorWindowNode::GetBackgroundPosition() const
{
	FVector2D NodeSize = GetSize();
	return FMargin(0.f, 0.f, NodeSize.X, NodeSize.Y);
}

FMargin SDisplayClusterConfiguratorWindowNode::GetAreaResizeHandlePosition() const
{
	FVector2D NodeSize = GetSize();
	return FMargin(NodeSize.X, NodeSize.Y, 0.f, 0.f);
}

EVisibility SDisplayClusterConfiguratorWindowNode::GetAreaResizeHandleVisibility() const
{
	if (IsClusterNodeLocked())
	{
		return EVisibility::Collapsed;
	}

	return GetSelectionVisibility();
}

bool SDisplayClusterConfiguratorWindowNode::IsAspectRatioFixed() const
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	return WindowEdNode->IsFixedAspectRatio();
}

FSlateColor SDisplayClusterConfiguratorWindowNode::GetCornerColor() const
{
	if (GetOwnerPanel()->SelectionManager.SelectedNodes.Contains(GraphNode))
	{
		return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Color.Selected");
	}

	if (IsClusterNodeLocked())
	{
		return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Window.Corner.Color.Locked");
	}

	return FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Window.Corner.Color");
}

FVector2D SDisplayClusterConfiguratorWindowNode::GetPreviewImageSize() const
{
	return GetSize();
}

EVisibility SDisplayClusterConfiguratorWindowNode::GetPreviewImageVisibility() const
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	const FString& PreviewImagePath = WindowEdNode->GetPreviewImagePath();
	return !PreviewImagePath.IsEmpty() ? EVisibility::Visible : EVisibility::Hidden;
}

bool SDisplayClusterConfiguratorWindowNode::CanShowInfoWidget() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	FVector2D NodeSize = GetSize();

	return IsNodeVisible() && OutputMapping->GetOutputMappingSettings().bShowWindowInfo && NodeSize > FVector2D::ZeroVector;
}

bool SDisplayClusterConfiguratorWindowNode::CanShowCornerImageWidget() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	FVector2D NodeSize = GetSize();

	return IsNodeVisible() && OutputMapping->GetOutputMappingSettings().bShowWindowCornerImage && NodeSize > FVector2D::ZeroVector;
}

bool SDisplayClusterConfiguratorWindowNode::IsClusterNodeLocked() const
{
	TSharedPtr<FDisplayClusterConfiguratorBlueprintEditor> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	return OutputMapping->GetOutputMappingSettings().bLockClusterNodes;
}

void SDisplayClusterConfiguratorWindowNode::OnPreviewImageChanged()
{
	if (PreviewImageWidget.IsValid())
	{
		UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
		PreviewImageWidget->SetImagePath(WindowEdNode->GetPreviewImagePath());
	}
}

#undef LOCTEXT_NAMESPACE