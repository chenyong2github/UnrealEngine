// Copyright Epic Games, Inc. All Rights Reserved.

#include "SRCPanelTreeNode.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RemoteControlPanelNode"

TSharedRef<SWidget> PanelTreeNode::MakeNodeWidget(const FMakeNodeWidgetArgs& Args)
{
	auto WidgetOrNull = [](const TSharedPtr<SWidget>& Widget) {return Widget ? Widget.ToSharedRef() : SNullWidget::NullWidget; };

	return SNew(SHorizontalBox)
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
		]
		// Field value
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		.FillWidth(1.0f)
		[
			WidgetOrNull(Args.ValueWidget)
		]
		// Unexpose button
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			WidgetOrNull(Args.UnexposeButton)
		];
}


#undef LOCTEXT_NAMESPACE /*RemoteControlPanelNode*/