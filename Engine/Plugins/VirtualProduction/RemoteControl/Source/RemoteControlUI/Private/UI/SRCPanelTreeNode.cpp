// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelTreeNode.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelNode"

TSharedRef<SWidget> SRCPanelTreeNode::MakeSplitRow(TSharedRef<SWidget> LeftColumn, TSharedRef<SWidget> RightColumn)
{
	TAttribute<float> LeftColumnAttribute;
	LeftColumnAttribute.BindRaw(this, &SRCPanelTreeNode::GetLeftColumnWidth);
	TAttribute<float> RightColumnAttribute;
	RightColumnAttribute.BindRaw(this, &SRCPanelTreeNode::GetRightColumnWidth);
	
	return SNew(SSplitter)
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(1.0f)
		.HitDetectionSplitterHandleSize(5.0f)
		+ SSplitter::Slot()
		.Value(MoveTemp(LeftColumnAttribute))
		.OnSlotResized(SSplitter::FOnSlotResized::CreateRaw(this, &SRCPanelTreeNode::OnLeftColumnResized))
		[
			LeftColumn
		]
		+ SSplitter::Slot()
		.Value(MoveTemp(RightColumnAttribute))
		.OnSlotResized(SSplitter::FOnSlotResized::CreateRaw(this, &SRCPanelTreeNode::SetColumnWidth))
		[
			RightColumn
		];
}

TSharedRef<SWidget> SRCPanelTreeNode::MakeNodeWidget(const FMakeNodeWidgetArgs& Args)
{
	auto WidgetOrNull = [](const TSharedPtr<SWidget>& Widget) {return Widget ? Widget.ToSharedRef() : SNullWidget::NullWidget; };

	TSharedRef<SWidget> LeftColumn = 
		SNew(SHorizontalBox)
		.Clipping(EWidgetClipping::OnDemand)
		// Drag and drop handle
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.DragHandle)
		]
		// Field name
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.NameWidget)
		]
		// Rename button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.RenameButton)
		];

	TSharedRef<SWidget> RightColumn =
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		[
			SNew(SBox)
			.HAlign(HAlign_Left)
			[
				WidgetOrNull(Args.ValueWidget)			
			]
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(0.f)
			[
				WidgetOrNull(Args.UnexposeButton)
			]
		];
		
	return MakeSplitRow(LeftColumn, RightColumn);
}

void SRCPanelTreeNode::OnLeftColumnResized(float) const
{
	// This has to be bound or the splitter will take it upon itself to determine the size
	// We do nothing here because it is handled by the column size data
}

float SRCPanelTreeNode::GetLeftColumnWidth() const
{
	const float Offset = GetType() == ENodeType::Group ? SplitterOffset : 0;
	return FMath::Clamp(ColumnSizeData.LeftColumnWidth.Get() + Offset, 0.f, 1.f);
}

float SRCPanelTreeNode::GetRightColumnWidth() const
{
	const float Offset = GetType() == ENodeType::Group ? SplitterOffset : 0;
	return  FMath::Clamp(ColumnSizeData.RightColumnWidth.Get() - Offset, 0.f, 1.f);
}

void SRCPanelTreeNode::SetColumnWidth(float InWidth)
{
	const float Offset = GetType() == ENodeType::Group ? SplitterOffset : -SplitterOffset;
	ColumnSizeData.SetColumnWidth(FMath::Clamp(InWidth + SplitterOffset, 0.f, 1.f));
}

#undef LOCTEXT_NAMESPACE /*RemoteControlPanelNode*/