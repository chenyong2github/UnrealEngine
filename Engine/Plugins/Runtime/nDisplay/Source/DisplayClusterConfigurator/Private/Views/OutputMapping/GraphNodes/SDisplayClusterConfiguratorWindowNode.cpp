// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorWindowNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Interfaces/Views/OutputMapping/IDisplayClusterConfiguratorViewOutputMapping.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorViewportNode.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorResizer.h"

#include "SGraphPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorWindowNode"

int32 const SDisplayClusterConfiguratorWindowNode::DefaultZOrder = 100;

class SCornerImage
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCornerImage)
		: _ColorAndOpacity(FLinearColor::White)
		, _Size(FVector2D(60.f))
	{ }

		SLATE_ARGUMENT(FSlateColor, ColorAndOpacity)

		SLATE_ARGUMENT(FVector2D, Size)

		/** Invoked when the mouse is pressed in the widget. */
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetOnMouseButtonDown(InArgs._OnMouseButtonDown);

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
};

class SNodeInfo
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNodeInfo)
	{ }
		SLATE_ARGUMENT(FString, NodeName)

		/** Invoked when the mouse is pressed in the widget. */
		SLATE_EVENT(FPointerEventHandler, OnMouseButtonDown)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<SDisplayClusterConfiguratorWindowNode>& InWindowNode)
	{
		SetOnMouseButtonDown(InArgs._OnMouseButtonDown);

		WindowEdNode = InWindowNode->GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();

		TitleSize = 0.2f;
		TitleParringX = 0.05f;

		ChildSlot
		[
			SAssignNew(TitleBox, SBox)
	
			.WidthOverride(this, &SNodeInfo::GetTitleWidth)
			.HeightOverride(this, &SNodeInfo::GetTitleHeight)
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				SAssignNew(TitleContent, SOverlay)
			]
		];

		TitleContent->AddSlot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			[
				InWindowNode->CreateBackground(FDisplayClusterConfiguratorStyle::GetColor("DisplayClusterConfigurator.Node.Window.Title.Background"))
			];

		TitleSlot = &TitleContent->AddSlot()
			.VAlign(VAlign_Fill)
			.HAlign(HAlign_Fill)
			.Padding(TAttribute<FMargin>(this, &SNodeInfo::GetTitlePadding))
			[
				SNew(SBox)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Fill)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					.FillWidth(0.3f)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						.StretchDirection(EStretchDirection::DownOnly)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						[
							SNew(STextBlock)
							.Text(this, &SNodeInfo::GetPositionAndSizeText)
							.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
							.Justification(ETextJustify::Center)
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Center)
					.FillWidth(0.4f)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						.StretchDirection(EStretchDirection::DownOnly)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(FText::FromString(InArgs._NodeName))
							.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Bold"))
							.Justification(ETextJustify::Center)
						]
					]
	
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Fill)
					.HAlign(HAlign_Fill)
					.FillWidth(0.3f)
					[
						SNew(SScaleBox)
						.Stretch(EStretch::ScaleToFit)
						.StretchDirection(EStretchDirection::DownOnly)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(this, &SNodeInfo::GetCfgHostText)
							.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
							.Justification(ETextJustify::Center)
						]
					]
				]
			];
	}

	FText GetPositionAndSizeText() const
	{
		check(WindowEdNode.IsValid());

		FDisplayClusterConfigurationRectangle WindowRect = WindowEdNode->GetCfgWindowRect();
		return FText::Format(LOCTEXT("ResAndOffset", "[{0} x {1}] @ {2}, {3}"), WindowRect.W, WindowRect.H, WindowRect.X, WindowRect.Y);
	}

	FMargin GetTitlePadding() const
	{
		check(WindowEdNode.IsValid());

		FDisplayClusterConfigurationRectangle WindowRect = WindowEdNode->GetCfgWindowRect();
		return FMargin(WindowRect.W * TitleParringX, 0.f);
	}

	FOptionalSize GetTitleWidth() const
	{
		check(WindowEdNode.IsValid());

		FDisplayClusterConfigurationRectangle WindowRect = WindowEdNode->GetCfgWindowRect();
		return WindowRect.W;
	}

	FOptionalSize GetTitleHeight() const
	{
		check(WindowEdNode.IsValid());

		FDisplayClusterConfigurationRectangle WindowRect = WindowEdNode->GetCfgWindowRect();
		return WindowRect.H * TitleSize;
	}

	FText GetCfgHostText() const
	{
		return FText::Format(LOCTEXT("IPAddress", "IP: {0}"), FText::FromString(WindowEdNode->GetCfgHost()));
	}

private:
	TWeakObjectPtr<UDisplayClusterConfiguratorWindowNode> WindowEdNode;

	TSharedPtr<SBox> TitleBox;

	TSharedPtr<SOverlay> TitleContent;

	SOverlay::FOverlaySlot* TitleSlot;

	float TitleSize;

	float TitleParringX;
};

void SDisplayClusterConfiguratorWindowNode::Construct(const FArguments& InArgs,
	UDisplayClusterConfiguratorWindowNode* InWindowNode,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InWindowNode, InToolkit);

	WindowScaleFactor = FVector2D(1, 1);

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
		SNew(SConstraintCanvas)
		.Visibility(this, &SDisplayClusterConfiguratorWindowNode::GetNodeVisibility)

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
					SNew(SBorder)
					.BorderImage(this, &SDisplayClusterConfiguratorWindowNode::GetBorderBrush)
				]
			]
		]

		+ SConstraintCanvas::Slot()
		.Offset(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorWindowNode::GetAreaResizeHandlePosition)))
		.AutoSize(true)
		.Alignment(FVector2D::ZeroVector)
		[
			SNew(SDisplayClusterConfiguratorResizer, ToolkitPtr.Pin().ToSharedRef(), SharedThis(this))
			.Visibility(this, &SDisplayClusterConfiguratorWindowNode::GetSelectionVisibility)
			.IsFixedAspectRatio(this, &SDisplayClusterConfiguratorWindowNode::IsAspectRatioFixed)
		]
	];
}

FVector2D SDisplayClusterConfiguratorWindowNode::ComputeDesiredSize(float) const
{
	return GetSize() * WindowScaleFactor;
}

FVector2D SDisplayClusterConfiguratorWindowNode::GetPosition() const
{
	return SDisplayClusterConfiguratorBaseNode::GetPosition() * WindowScaleFactor;
}

void SDisplayClusterConfiguratorWindowNode::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter)
{
	const FVector2D CurrentPosition = GetPosition();
	const FVector2D Offset = NewPosition - CurrentPosition;

	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	FVector2D BestOffset = WindowEdNode->FindNonOverlappingOffsetFromParent(Offset);

	SGraphNode::MoveTo(CurrentPosition + BestOffset, NodeFilter);

	WindowEdNode->UpdateObject();
	WindowEdNode->UpdateChildPositions(BestOffset);
}

TArray<FOverlayWidgetInfo> SDisplayClusterConfiguratorWindowNode::GetOverlayWidgets(bool bSelected, const FVector2D& WidgetSize) const
{
	TArray<FOverlayWidgetInfo> Widgets = SGraphNode::GetOverlayWidgets(bSelected, WidgetSize);

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

UObject* SDisplayClusterConfiguratorWindowNode::GetEditingObject() const
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	return WindowEdNode->GetObject();
}

void SDisplayClusterConfiguratorWindowNode::SetNodeSize(const FVector2D InLocalSize, bool bFixedAspectRatio)
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	FVector2D BestSize = WindowEdNode->FindNonOverlappingSizeFromParent(InLocalSize, bFixedAspectRatio);

	GraphNode->ResizeNode(BestSize);
}

void SDisplayClusterConfiguratorWindowNode::OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	UObject* SelectedObject = InTreeItem->GetObject();

	if (UObject* NodeObject = GetEditingObject())
	{
		if (NodeObject == SelectedObject)
		{
			bIsObjectFocused = true;
			return;
		}
		else
		{
			// In case we calling parent from the child
			if (TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = InTreeItem->GetParent())
			{
				if (UObject* ParentItemObject = ParentItem->GetObject())
				{
					if (NodeObject == ParentItemObject)
					{
						bIsObjectFocused = true;
						return;
					}
				}
			}

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

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateCornerImageWidget()
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();

	return SNew(SCornerImage)
		.ColorAndOpacity(WindowEdNode->CornerColor)
		.OnMouseButtonDown_Lambda([this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
		{
			ExecuteMouseButtonDown(MouseEvent);
			return FReply::Handled();
		});
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateInfoWidget()
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();

	return SNew(SNodeInfo, SharedThis(this))
		.NodeName(WindowEdNode->GetNodeName())
		.OnMouseButtonDown_Lambda([this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
		{
			ExecuteMouseButtonDown(MouseEvent);
			return FReply::Handled();
		});
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateBackground(const TAttribute<FSlateColor>& InColorAndOpacity)
{
	return SNew(SOverlay)
		+SOverlay::Slot()
		.VAlign(VAlign_Fill)
		.HAlign(HAlign_Fill)
		.Padding(0.f)
		[
			SNew(SImage)
			.ColorAndOpacity(InColorAndOpacity)
			.Image(FDisplayClusterConfiguratorStyle::GetBrush("DisplayClusterConfigurator.Node.Body"))
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

bool SDisplayClusterConfiguratorWindowNode::IsAspectRatioFixed() const
{
	UDisplayClusterConfiguratorWindowNode* WindowEdNode = GetGraphNodeChecked<UDisplayClusterConfiguratorWindowNode>();
	return WindowEdNode->IsFixedAspectRatio();
}

bool SDisplayClusterConfiguratorWindowNode::CanShowInfoWidget() const
{
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	FVector2D NodeSize = GetSize();

	return IsNodeVisible() && OutputMapping->IsShowWindowInfo() && NodeSize > FVector2D::ZeroVector;
}

bool SDisplayClusterConfiguratorWindowNode::CanShowCornerImageWidget() const
{
	TSharedPtr<FDisplayClusterConfiguratorToolkit> Toolkit = ToolkitPtr.Pin();
	check(Toolkit.IsValid());

	TSharedRef<IDisplayClusterConfiguratorViewOutputMapping> OutputMapping = Toolkit->GetViewOutputMapping();
	FVector2D NodeSize = GetSize();

	return IsNodeVisible() && OutputMapping->IsShowWindowCornerImage() && NodeSize > FVector2D::ZeroVector;
}

#undef LOCTEXT_NAMESPACE