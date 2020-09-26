// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/OutputMapping/GraphNodes/SDisplayClusterConfiguratorWindowNode.h"

#include "DisplayClusterConfiguratorStyle.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfigurationTypes.h"
#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"
#include "Views/OutputMapping/EdNodes/DisplayClusterConfiguratorWindowNode.h"
#include "Views/OutputMapping/Slots/DisplayClusterConfiguratorOutputMappingWindowSlot.h"
#include "Views/OutputMapping/Widgets/SDisplayClusterConfiguratorResizer.h"

#include "SGraphPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScaleBox.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorWindowNode"

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

		UDisplayClusterConfigurationClusterNode* CfgClusterNode = InWindowNode->CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);
		CfgClusterNodePtr = CfgClusterNode;

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
							.Text(FText::Format(LOCTEXT("IPAddress", "IP: {0}"), FText::FromString(CfgClusterNode->Host)))
							.TextStyle(&FDisplayClusterConfiguratorStyle::GetWidgetStyle<FTextBlockStyle>("DisplayClusterConfigurator.Node.Text.Regular"))
							.Justification(ETextJustify::Center)
						]
					]
				]
			];
	}

	FText GetPositionAndSizeText() const
	{
		UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);

		return FText::Format(LOCTEXT("ResAndOffset", "[{0} x {1}] @ {2}, {3}"), CfgClusterNode->WindowRect.W, CfgClusterNode->WindowRect.H, CfgClusterNode->WindowRect.X, CfgClusterNode->WindowRect.Y);
	}

	FMargin GetTitlePadding() const
	{
		UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);

		return FMargin(CfgClusterNode->WindowRect.W * TitleParringX, 0.f);
	}

	FOptionalSize GetTitleWidth() const
	{
		UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);

		return CfgClusterNode->WindowRect.W;
	}

	FOptionalSize GetTitleHeight() const
	{
		UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
		check(CfgClusterNode != nullptr);

		return CfgClusterNode->WindowRect.H * TitleSize;
	}

private:
	TWeakObjectPtr<UDisplayClusterConfigurationClusterNode> CfgClusterNodePtr;

	TSharedPtr<SBox> TitleBox;

	TSharedPtr<SOverlay> TitleContent;

	SOverlay::FOverlaySlot* TitleSlot;

	float TitleSize;

	float TitleParringX;
};

void SDisplayClusterConfiguratorWindowNode::Construct(const FArguments& InArgs,
	UDisplayClusterConfiguratorWindowNode* InWindowNode,
	const TSharedRef<FDisplayClusterConfiguratorOutputMappingWindowSlot>& InWindowSlot,
	const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
{
	SDisplayClusterConfiguratorBaseNode::Construct(SDisplayClusterConfiguratorBaseNode::FArguments(), InWindowNode, InToolkit);

	WindowSlotPtr = InWindowSlot;
	WindowNodePtr = InWindowNode;
	CfgClusterNodePtr = InWindowNode->GetCfgClusterNode();

	UpdateGraphNode();
}

void SDisplayClusterConfiguratorWindowNode::UpdateGraphNode()
{
	SDisplayClusterConfiguratorBaseNode::UpdateGraphNode();

	TSharedPtr<FDisplayClusterConfiguratorOutputMappingWindowSlot> WindowSlot = WindowSlotPtr.Pin();
	check(WindowSlot.IsValid());

	FVector2D LocalSize = WindowSlot->GetLocalSize();

	NodeSlot = &GetOrAddSlot(ENodeZone::Center)
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
				.WidthOverride(LocalSize.X)
				.HeightOverride(LocalSize.Y)
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
			]
		];


	NodeSlot->GetWidget()->SetVisibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &SDisplayClusterConfiguratorWindowNode::GetNodeVisibility)));

	NodeSlot->SlotSize(FVector2D(LocalSize.X, LocalSize.Y));
}

UObject* SDisplayClusterConfiguratorWindowNode::GetEditingObject() const
{
	return WindowNodePtr->GetObject();
}

void SDisplayClusterConfiguratorWindowNode::SetNodePositionOffset(const FVector2D InLocalOffset)
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingWindowSlot> WindowSlot = WindowSlotPtr.Pin();
	check(WindowSlot.IsValid());

	WindowSlot->SetLocalPosition(WindowSlot->GetLocalPosition() + InLocalOffset);
}

void SDisplayClusterConfiguratorWindowNode::SetNodeSize(const FVector2D InLocalSize)
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingWindowSlot> WindowSlot = WindowSlotPtr.Pin();
	check(WindowSlot.IsValid());

	NodeSlotBox->SetWidthOverride(InLocalSize.X);
	NodeSlotBox->SetHeightOverride(InLocalSize.Y);

	NodeSlot->SlotSize(InLocalSize);

	WindowSlot->SetLocalSize(InLocalSize);
}

void SDisplayClusterConfiguratorWindowNode::OnSelectedItemSet(const TSharedRef<IDisplayClusterConfiguratorTreeItem>& InTreeItem)
{
	UObject* SelectedObject = InTreeItem->GetObject();

	if (UObject* NodeObject = GetEditingObject())
	{
		if (NodeObject == SelectedObject)
		{
			InNodeVisibile = true;
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
						InNodeVisibile = true;
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
					InNodeVisibile = true;
					return;
				}
			}
		}
	}

	InNodeVisibile = false;
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::GetCornerImageWidget()
{
	return SNew(SCornerImage)
		.ColorAndOpacity(WindowNodePtr.Get()->CornerColor)
		.OnMouseButtonDown_Lambda([this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
		{
			ExecuteMouseButtonDown(MouseEvent);
			return FReply::Handled();
		});
}

TSharedRef<SWidget> SDisplayClusterConfiguratorWindowNode::CreateInfoWidget()
{
	TSharedPtr<FDisplayClusterConfiguratorOutputMappingWindowSlot> WindowSlot = WindowSlotPtr.Pin();
	check(WindowSlot.IsValid());

	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	UDisplayClusterConfiguratorWindowNode* WindowNode = WindowNodePtr.Get();
	check(WindowNode != nullptr);

	FVector2D WindowPos(CfgClusterNode->WindowRect.X, CfgClusterNode->WindowRect.Y);
	FVector2D WindowSize(CfgClusterNode->WindowRect.W, CfgClusterNode->WindowRect.H);
	
	FVector2D LocalSize = WindowSlot->GetLocalSize();

	return SNew(SNodeInfo, SharedThis(this))
		.NodeName(WindowNode->GetNodeName())
		.OnMouseButtonDown_Lambda([this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
		{
			ExecuteMouseButtonDown(MouseEvent);
			return FReply::Handled();
		});
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

FMargin SDisplayClusterConfiguratorWindowNode::GetAreaResizeHandlePosition() const
{
	UDisplayClusterConfigurationClusterNode* CfgClusterNode = CfgClusterNodePtr.Get();
	check(CfgClusterNode != nullptr);

	return FMargin(CfgClusterNode->WindowRect.W, CfgClusterNode->WindowRect.H, 0.f, 0.f);
}

#undef LOCTEXT_NAMESPACE