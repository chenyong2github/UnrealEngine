// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAnimNodeReference.h"

#include "SLevelOfDetailBranchNode.h"
#include "EditorStyleSet.h"
#include "K2Node_AnimNodeReference.h"
#include "SGraphPin.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SAnimNodeReferenceNode"

void SAnimNodeReference::Construct(const FArguments& InArgs, UK2Node_AnimNodeReference* InNode)
{
	GraphNode = InNode;
	UpdateGraphNode();
}

TSharedRef<SWidget> SAnimNodeReference::UpdateTitleWidget(FText InTitleText, TSharedPtr<SWidget> InTitleWidget, EHorizontalAlignment& InOutTitleHAlign, FMargin& InOutTitleMargin) const
{
	UK2Node_AnimNodeReference* K2Node_AnimNodeReference = CastChecked<UK2Node_AnimNodeReference>(GraphNode);

	InTitleWidget =
		SNew(SLevelOfDetailBranchNode)
		.UseLowDetailSlot(this, &SAnimNodeReference::UseLowDetailNodeTitles)
		.LowDetail()
		[
			SNew(SSpacer)
		]
		.HighDetail()
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([K2Node_AnimNodeReference]()
				{
					return K2Node_AnimNodeReference->GetLabelText();
				})
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NodeSubTitle", "Anim Node Reference"))
				.TextStyle(&FEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("AnimGraph.AnimNodeReference.Subtitle"))
			]
		];

	InOutTitleHAlign = HAlign_Left;
	InOutTitleMargin = FMargin(12.0f, 8.0f, 36.0f, 6.0f);
	
	return InTitleWidget.ToSharedRef();
}

TSharedPtr<SGraphPin> SAnimNodeReference::CreatePinWidget(UEdGraphPin* Pin) const
{
	TSharedPtr<SGraphPin> DefaultWidget = SGraphNodeK2Var::CreatePinWidget(Pin);
	DefaultWidget->SetShowLabel(false);

	return DefaultWidget;
}

#undef LOCTEXT_NAMESPACE